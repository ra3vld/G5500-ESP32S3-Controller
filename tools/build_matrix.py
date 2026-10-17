#!/usr/bin/env python3
"""Build all 54 hardware configurations with Arduino CLI and/or PlatformIO (no upload)."""
import argparse
import concurrent.futures
import hashlib
import fcntl
import itertools
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time
ROOT=Path(__file__).resolve().parents[1]
PINNED_PLATFORM='https://github.com/pioarduino/platform-espressif32/releases/download/55.03.311/platform-espressif32.zip'
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--tool',choices=['all','arduino','platformio'],default='all')
p.add_argument('--arduino-cli',default=os.environ.get('ARDUINO_CLI','arduino-cli'))
p.add_argument('--arduino-config',default=os.environ.get('ARDUINO_CONFIG_FILE'))
p.add_argument('--arduino-vendor',default='esp32')
p.add_argument('--pio',default=os.environ.get('PIO','pio'))
p.add_argument('--platform',default=PINNED_PLATFORM)
p.add_argument('--library-dir',type=Path,help='Local verified pinned libraries (offline PlatformIO builds)')
p.add_argument('--workers',type=int,default=2)
p.add_argument('--jobs',type=int,default=4)
p.add_argument('--simulation', choices=['0', '1'], help='Explicit drive simulation override; leave editable defaults unchanged')
p.add_argument('--legacy',action='store_true',help='Also compile the original S2/S3 named environments')
p.add_argument('--select',choices=['all','lcd','smoke'],default='all',help='Optional LCD-only or three-display regression check')
p.add_argument('--boards',nargs='+',choices=['s3','s2-mini','esp32'],default=['s3','s2-mini','esp32'])
p.add_argument('--run-name',help='Separate result/build directory for a focused validation run')
a=p.parse_args()
if a.run_name and not re.fullmatch(r'[A-Za-z0-9_-]+',a.run_name):p.error('Invalid run name')
out=ROOT/'artifacts/build-matrix';work_root=ROOT/'build/matrix'
if a.run_name:out=out/a.run_name;work_root=work_root/a.run_name
out.mkdir(parents=True,exist_ok=True)
locks=[]
for name in (['arduino','platformio'] if a.tool=='all' else [a.tool]):
    handle=(out/(name+'.lock')).open('a')
    try:fcntl.flock(handle,fcntl.LOCK_EX|fcntl.LOCK_NB)
    except BlockingIOError:raise SystemExit('Another '+name+' matrix is running; finish it first')
    locks.append(handle)
source=ROOT/'G5500_Fork'
source_hash=hashlib.sha256(b''.join(f.name.encode()+f.read_bytes() for f in sorted(source.iterdir()) if f.suffix in {'.ino','.c','.cpp','.hpp','.h','.csv'} and not f.name.endswith('.ino.cpp'))).hexdigest()
frozen=ROOT/'build/matrix/sources'/source_hash/a.tool/'G5500_Fork'
frozen.mkdir(parents=True,exist_ok=True)
for f in source.iterdir():
    if f.suffix in {'.ino','.c','.cpp','.hpp','.h','.csv'} and not f.name.endswith('.ino.cpp'): shutil.copy2(f,frozen/f.name)
source=frozen
combinations=list(itertools.product(a.boards,range(3),range(2),range(3)))
if a.select=='lcd': combinations=[case for case in combinations if case[1]==2]
if a.select=='smoke': combinations=[case for case in combinations if case[2:]==(1,0)]
if a.legacy: combinations += [('s2',0,1,0),('s2',1,1,0)]

def build(tool,board,display,bt,drive):
    name=f'{tool}-{board}-d{display}-b{bt}-m{drive}'
    work=work_root/name;work.mkdir(parents=True,exist_ok=True)
    flags=f'-DBOARD_PROFILE={dict(s3=1,s2=2,**{"s2-mini":3,"esp32":4})[board]} -DDISPLAY_KIND={display} -DTFT_DISPLAY_ENABLED={int(display==1)} -DBLUETOOTH_ENABLED={bt} -DDRIVE_INTERFACE={drive}'
    if a.simulation is not None: flags += ' -DDRIVE_SIMULATION='+a.simulation
    app_size = 0x640000 if board=='s3' else 0x300000 if board=='esp32' else 0x1e0000
    partition_file = 'partitions_s3.csv' if board=='s3' else 'partitions_esp32.csv' if board=='esp32' else 'partitions_s2.csv'
    if tool=='arduino':
        sketch=work/'G5500_Fork';sketch.mkdir(exist_ok=True)
        for f in source.iterdir():
            if f.suffix in {'.ino','.c','.cpp','.hpp','.h'} and not f.name.endswith('.ino.cpp'): shutil.copy2(f,sketch/f.name)
        shutil.copy2(source/partition_file,sketch/'partitions.csv')
        fqbn=f'{a.arduino_vendor}:esp32:'+('esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=custom,USBMode=hwcdc,CDCOnBoot=cdc,FlashMode=qio' if board=='s3' else 'lolin_s2_mini:CDCOnBoot=default' if board=='s2-mini' else 'esp32:FlashSize=4M,PSRAM=disabled,PartitionScheme=huge_app,FlashMode=dio,FlashFreq=40,DebugLevel=none' if board=='esp32' else 'esp32s2:FlashSize=4M,PSRAM=disabled,PartitionScheme=min_spiffs,CDCOnBoot=cdc,UploadMode=cdc')
        cmd=[a.arduino_cli]
        if a.arduino_config:cmd+=['--config-file',a.arduino_config]
        cmd+=['compile','--jobs',str(a.jobs),'--fqbn',fqbn,'--build-property','compiler.cpp.extra_flags='+flags,'--build-property','upload.maximum_size='+str(app_size),'--build-path',str(work/'objects'),'--output-dir',str(work/'output'),str(sketch)]
        for cache in (work/'objects').glob('*.cache'):
            if cache.stat().st_size == 0: cache.unlink()
        binary=work/'output/G5500_Fork.ino.bin'
    else:
        snapshot=work/'G5500_Fork';snapshot.mkdir(exist_ok=True)
        for f in source.iterdir():
            if f.suffix in {'.ino','.c','.cpp','.hpp','.h','.csv'} and not f.name.endswith('.ino.cpp'): shutil.copy2(f,snapshot/f.name)
        config=work/'platformio.ini'
        extra='-DBOARD_HAS_PSRAM -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1' if board=='s3' else '-DARDUINO_USB_MODE=0 -DARDUINO_USB_CDC_ON_BOOT='+('0' if board=='esp32' else '1')+(' -DBOARD_HAS_PSRAM' if board=='s2-mini' else '')
        memory='board_build.arduino.memory_type = qio_opi\nboard_build.psram_type = opi\n' if board=='s3' else ''
        deps=''
        if display:
            if a.library_dir:deps='lib_extra_dirs = '+str(a.library_dir.resolve())+'\n'
            elif display==2:deps='lib_deps = arduino-libraries/LiquidCrystal @ 1.0.7\n'
            else:deps='lib_deps =\n adafruit/Adafruit GFX Library @ 1.12.1\n adafruit/Adafruit ILI9341 @ 1.6.2\n adafruit/Adafruit BusIO @ 1.17.2\n https://github.com/PaulStoffregen/XPT2046_Touchscreen/archive/refs/tags/v1.4.zip\n'
        config.write_text(f'[platformio]\nsrc_dir = {snapshot}\nbuild_dir = {work}/objects\nlibdeps_dir = {work}/libdeps\n[env:matrix]\nplatform = {a.platform}\nboard = '+('esp32-s3-devkitc-1' if board=='s3' else 'lolin_s2_mini' if board=='s2-mini' else 'esp32dev' if board=='esp32' else 'esp32-s2-saola-1')+f'\nframework = arduino\nboard_build.flash_mode = '+('dio' if board in ('s2-mini','esp32') else 'qio')+('\nboard_build.f_flash = 40000000L' if board=='esp32' else '')+f'\nboard_upload.flash_size = '+('16MB' if board=='s3' else '4MB')+f'\nboard_build.partitions = {source}/'+partition_file+'\n'+memory+'build_flags = '+extra+' '+flags+'\nlib_ldf_mode = chain+\n'+deps)
        cmd=[a.pio,'run','-c',str(config),'-e','matrix','-j',str(a.jobs)]
        binary=work/'objects/matrix/firmware.bin'
    child_env=os.environ.copy()
    if tool=="arduino": child_env["ARDUINO_BUILD_CACHE_PATH"]=str(work/"core-cache")
    started=time.monotonic()
    with (out/(name+'.log')).open('w') as log:
        result=subprocess.run(cmd,cwd=ROOT,env=child_env,stdout=log,stderr=subprocess.STDOUT)
    text=(out/(name+'.log')).read_text()
    ram=re.search(r'(?:Global variables use (\d+)|RAM:.*?used (\d+))',text)
    flash=re.search(r'(?:Sketch uses (\d+)|Flash:.*?used (\d+))',text)
    item=dict(tool=tool,board=board,display=display,bluetooth=bt,drive=drive,simulation=a.simulation,appSlotBytes=app_size,ok=result.returncode==0 and binary.exists() and binary.stat().st_size <= app_size,seconds=round(time.monotonic()-started,1),binaryBytes=binary.stat().st_size if binary.exists() else None,flashBytes=int(next(x for x in flash.groups() if x)) if flash else None,ramBytes=int(next(x for x in ram.groups() if x)) if ram else None,log=str(out/(name+'.log')))
    print(('PASS' if item['ok'] else 'FAIL')+' '+name+f' ({item["seconds"]}s)',flush=True)
    return item

tools=['arduino','platformio'] if a.tool=='all' else [a.tool]
# Both tools build private source snapshots, avoiding competing generated .ino.cpp writers.
results=[]
for tool in tools:
    workers=a.workers
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        futures=[pool.submit(build,tool,*case) for case in combinations]
        for future in concurrent.futures.as_completed(futures):
            results.append(future.result())
            (out/f'{tool}.json').write_text(json.dumps(dict(sourceSha256=source_hash,results=[r for r in results if r['tool']==tool]),indent=2)+'\n')
if any(not r['ok'] for r in results):raise SystemExit(1)
print(f'PASS: {len(results)} builds; results in {out}')
