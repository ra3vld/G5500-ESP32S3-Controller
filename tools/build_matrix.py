#!/usr/bin/env python3
"""Build all 36 hardware configurations with Arduino CLI and/or PlatformIO (no upload)."""
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
p.add_argument('--legacy',action='store_true',help='Also compile the original S2/S3 named environments')
p.add_argument('--select',choices=['all','lcd'],default='all',help='Optional LCD-only check before the complete matrix')
a=p.parse_args()
out=ROOT/'artifacts/build-matrix';out.mkdir(parents=True,exist_ok=True)
locks=[]
for name in (['arduino','platformio'] if a.tool=='all' else [a.tool]):
    handle=(out/(name+'.lock')).open('a')
    try:fcntl.flock(handle,fcntl.LOCK_EX|fcntl.LOCK_NB)
    except BlockingIOError:raise SystemExit('Another '+name+' matrix is running; finish it first')
    locks.append(handle)
source=ROOT/'G5500_Fork'
source_hash=hashlib.sha256(b''.join(f.name.encode()+f.read_bytes() for f in sorted(source.iterdir()) if f.suffix in {'.ino','.cpp','.hpp','.h','.csv'} and not f.name.endswith('.ino.cpp'))).hexdigest()
frozen=ROOT/'build/matrix/sources'/source_hash/a.tool/'G5500_Fork'
frozen.mkdir(parents=True,exist_ok=True)
for f in source.iterdir():
    if f.suffix in {'.ino','.cpp','.hpp','.h','.csv'} and not f.name.endswith('.ino.cpp'): shutil.copy2(f,frozen/f.name)
source=frozen
combinations=list(itertools.product(('s3','s2-mini'),range(3),range(2),range(3)))
if a.select=='lcd': combinations=[case for case in combinations if case[1]==2]
if a.legacy: combinations += [('s2',0,1,0),('s2',1,1,0)]

def build(tool,board,display,bt,drive):
    name=f'{tool}-{board}-d{display}-b{bt}-m{drive}'
    work=ROOT/'build/matrix'/name;work.mkdir(parents=True,exist_ok=True)
    flags=f'-DBOARD_PROFILE={1 if board=="s3" else 3 if board=="s2-mini" else 2} -DDISPLAY_KIND={display} -DTFT_DISPLAY_ENABLED={int(display==1)} -DBLUETOOTH_ENABLED={bt} -DDRIVE_INTERFACE={drive}'
    if tool=='arduino':
        sketch=work/'G5500_Fork';sketch.mkdir(exist_ok=True)
        for f in source.iterdir():
            if f.suffix in {'.ino','.cpp','.hpp','.h'} and not f.name.endswith('.ino.cpp'): shutil.copy2(f,sketch/f.name)
        shutil.copy2(source/('partitions.csv' if board=='s3' else 'partitions_s2.csv'),sketch/'partitions.csv')
        fqbn=f'{a.arduino_vendor}:esp32:'+('esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=custom,USBMode=hwcdc,CDCOnBoot=cdc,FlashMode=qio' if board=='s3' else 'lolin_s2_mini:CDCOnBoot=default' if board=='s2-mini' else 'esp32s2:FlashSize=4M,PSRAM=disabled,PartitionScheme=min_spiffs,CDCOnBoot=cdc,UploadMode=cdc')
        cmd=[a.arduino_cli]
        if a.arduino_config:cmd+=['--config-file',a.arduino_config]
        cmd+=['compile','--jobs',str(a.jobs),'--fqbn',fqbn,'--build-property','compiler.cpp.extra_flags='+flags,'--build-property','upload.maximum_size='+str(0x640000 if board=='s3' else 0x1e0000),'--build-path',str(work/'objects'),'--output-dir',str(work/'output'),str(sketch)]
        for cache in (work/'objects').glob('*.cache'):
            if cache.stat().st_size == 0: cache.unlink()
        binary=work/'output/G5500_Fork.ino.bin'
    else:
        snapshot=work/'G5500_Fork';snapshot.mkdir(exist_ok=True)
        for f in source.iterdir():
            if f.suffix in {'.ino','.cpp','.hpp','.h','.csv'} and not f.name.endswith('.ino.cpp'): shutil.copy2(f,snapshot/f.name)
        config=work/'platformio.ini'
        extra='-DBOARD_HAS_PSRAM -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1' if board=='s3' else '-DARDUINO_USB_MODE=0 -DARDUINO_USB_CDC_ON_BOOT=1'+(' -DBOARD_HAS_PSRAM' if board=='s2-mini' else '')
        memory='board_build.arduino.memory_type = qio_opi\nboard_build.psram_type = opi\n' if board=='s3' else ''
        deps=''
        if display:
            if a.library_dir:deps='lib_extra_dirs = '+str(a.library_dir.resolve())+'\n'
            elif display==2:deps='lib_deps = arduino-libraries/LiquidCrystal @ 1.0.7\n'
            else:deps='lib_deps =\n adafruit/Adafruit GFX Library @ 1.12.1\n adafruit/Adafruit ILI9341 @ 1.6.2\n adafruit/Adafruit BusIO @ 1.17.2\n https://github.com/PaulStoffregen/XPT2046_Touchscreen/archive/refs/tags/v1.4.zip\n'
        config.write_text(f'[platformio]\nsrc_dir = {snapshot}\nbuild_dir = {work}/objects\nlibdeps_dir = {work}/libdeps\n[env:matrix]\nplatform = {a.platform}\nboard = '+('esp32-s3-devkitc-1' if board=='s3' else 'lolin_s2_mini' if board=='s2-mini' else 'esp32-s2-saola-1')+f'\nframework = arduino\nboard_build.flash_mode = '+('dio' if board=='s2-mini' else 'qio')+f'\nboard_upload.flash_size = '+('16MB' if board=='s3' else '4MB')+f'\nboard_build.partitions = {source}/'+('partitions.csv' if board=='s3' else 'partitions_s2.csv')+'\n'+memory+'build_flags = '+extra+' '+flags+'\nlib_ldf_mode = chain+\n'+deps)
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
    item=dict(tool=tool,board=board,display=display,bluetooth=bt,drive=drive,ok=result.returncode==0 and binary.exists() and binary.stat().st_size <= (0x640000 if board=='s3' else 0x1e0000),seconds=round(time.monotonic()-started,1),binaryBytes=binary.stat().st_size if binary.exists() else None,flashBytes=int(next(x for x in flash.groups() if x)) if flash else None,ramBytes=int(next(x for x in ram.groups() if x)) if ram else None,log=str(out/(name+'.log')))
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
