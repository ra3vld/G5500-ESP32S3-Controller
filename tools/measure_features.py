#!/usr/bin/env python3
"""Compare identical firmware snapshots with individual compile-time capabilities removed.

Results and logs stay in ignored local-notes/. Reuses each profile's PlatformIO
objects and immutable JSON results; never uploads. --platform supports a local
copy of the pinned platform for offline builds.
"""
import argparse
import csv
import configparser
import io
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import time

ROOT=Path(__file__).resolve().parents[1]
FLAGS={
 'DISPLAY_SUPPORT':{},'LOCAL_INPUT_SUPPORT':{},'WIFI_SUPPORT':{'WEB_INTERFACE_ENABLED':0,'WEB_LIVE_SUPPORT':0,'HUD_UDP_SUPPORT':0,'MQTT_SUPPORT':0,'HOME_ASSISTANT_DISCOVERY_SUPPORT':0,'ARDUINO_OTA_SUPPORT':0,'NTP_SUPPORT':0,'RSSI_HISTORY_SUPPORT':0},
 'ETHERNET_SUPPORT':{},'WEB_INTERFACE_ENABLED':{'WEB_LIVE_SUPPORT':0,'CAMERA_ENABLED':0},'WEB_LIVE_SUPPORT':{},
 'BLUETOOTH_SUPPORT':{'BLUETOOTH_SPP_SUPPORT':0},'BLUETOOTH_SPP_SUPPORT':{},'GS232_SUPPORT':{},'ROTCTLD_SUPPORT':{},
 'N1MM_SUPPORT':{},'PSTROTATOR_SUPPORT':{},'COMMAND_API_SUPPORT':{},'HUD_SUPPORT':{'HUD_UDP_SUPPORT':0,'HUD_UART_SUPPORT':0},
 'HUD_UDP_SUPPORT':{},'HUD_UART_SUPPORT':{},'RS485_SUPPORT':{'MODBUS_RTU_SUPPORT':0},'MODBUS_RTU_SUPPORT':{},
 'OTA_SUPPORT':{'ARDUINO_OTA_SUPPORT':0},'ARDUINO_OTA_SUPPORT':{},'CAMERA_ENABLED':{},'LOG_HISTORY_SUPPORT':{},
 'RSSI_HISTORY_SUPPORT':{},'RESET_HISTORY_SUPPORT':{},'NTP_SUPPORT':{},'MQTT_SUPPORT':{'HOME_ASSISTANT_DISCOVERY_SUPPORT':0},
 'HOME_ASSISTANT_DISCOVERY_SUPPORT':{},
 'GROUP_SERVICES':dict(DISPLAY_SUPPORT=0,LOCAL_INPUT_SUPPORT=0,WEB_INTERFACE_ENABLED=0,WEB_LIVE_SUPPORT=0,CAMERA_ENABLED=0,
    BLUETOOTH_SUPPORT=0,BLUETOOTH_SPP_SUPPORT=0,HUD_SUPPORT=0,HUD_UDP_SUPPORT=0,HUD_UART_SUPPORT=0,RS485_SUPPORT=0,MODBUS_RTU_SUPPORT=0,
    OTA_SUPPORT=0,ARDUINO_OTA_SUPPORT=0,LOG_HISTORY_SUPPORT=0,RSSI_HISTORY_SUPPORT=0,RESET_HISTORY_SUPPORT=0,NTP_SUPPORT=0,
    MQTT_SUPPORT=0,HOME_ASSISTANT_DISCOVERY_SUPPORT=0,N1MM_SUPPORT=0,PSTROTATOR_SUPPORT=0,ROTCTLD_SUPPORT=0),
}

def digest(paths):
 h=hashlib.sha256()
 for p in sorted(paths):h.update(str(p.relative_to(ROOT)).encode()+b'\0'+p.read_bytes())
 return h.hexdigest()

def sections(elf):
 # ELF32 little-endian section table; no compiler-specific output parsing.
 b=elf.read_bytes()
 if b[:6]!=b'\x7fELF\x01\x01':raise ValueError('Expected ELF32 little endian')
 offset=struct.unpack_from('<I',b,32)[0];size,count,names=struct.unpack_from('<HHH',b,46)
 entries=[struct.unpack_from('<10I',b,offset+i*size) for i in range(count)]
 n=entries[names];strings=b[n[4]:n[4]+n[5]];out={}
 for e in entries:
  name=strings[e[0]:].split(b'\0',1)[0].decode()
  if e[2]&2:out[name]=e[5]
 return out

def app_slot(partition):
 b=partition.read_bytes();slots=[]
 for pos in range(0,len(b)-31,32):
  magic,kind,subtype,offset,size=struct.unpack_from('<HBBII',b,pos)
  if magic==0x50aa and kind==0:slots.append(size)
 if not slots:raise ValueError('No app slots in compiled partitions.bin')
 return min(slots)

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--profiles',nargs='+',default=['esp32_3248s035c','esp32s3_n16r8'])
 p.add_argument('--flags',nargs='+',choices=['baseline',*FLAGS],default=list(FLAGS))
 p.add_argument('--pio',default=os.environ.get('PIO','pio'));p.add_argument('--platform')
 p.add_argument('--jobs',type=int,default=4)
 p.add_argument('--output',type=Path,default=ROOT/'local-notes/feature-memory')
 p.add_argument('--defines',nargs='*',default=[],help='Shared baseline overrides NAME=VALUE')
 a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True)
 if not a.output.is_relative_to(ROOT/'local-notes'):p.error('Keep measurements in local-notes/')
 for name in a.profiles:
  if not re.fullmatch('[a-zA-Z0-9_]+',name):p.error('Invalid profile')
 shared={}
 for definition in a.defines:
  if not re.fullmatch('[A-Z_0-9]+=[01]',definition):p.error('Invalid override')
  name,value=definition.split('=');shared[name]=int(value)
 source=ROOT/'G5500_Fork'
 paths=[x for x in source.rglob('*') if x.is_file() and x.suffix in {'.ino','.cpp','.hpp','.h','.c','.csv'} and not x.name.endswith('.ino.cpp')]
 source_hash=digest(paths);identity=digest([*paths,ROOT/'platformio.ini'])
 root=a.output/identity[:16];root.mkdir(exist_ok=True)
 frozen=ROOT/'build/feature-memory/sources'/source_hash/'G5500_Fork'
 frozen.mkdir(parents=True,exist_ok=True)
 for f in paths:
  out=frozen/f.relative_to(source);out.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(f,out)
 version=subprocess.check_output([a.pio,'--version'],text=True).strip()
 packages=[]
 core=Path(os.environ.get('PLATFORMIO_CORE_DIR',str(Path.home()/'.platformio')))
 for f in sorted((core/'packages').glob('*/package.json')):
  meta=json.loads(f.read_text());packages.append(dict(name=meta.get('name'),version=meta.get('version')))
 manifest=dict(sourceSha256=source_hash,identity=identity,revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
  utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),platformio=version,platform=a.platform,packages=packages,sharedDefines=shared)
 (root/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
 results=[]
 for profile in a.profiles:
  baseline=None
  for flag in dict.fromkeys(['baseline',*a.flags]):
   definitions=dict(shared)
   if flag!='baseline':
    definitions.update(FLAGS[flag])
    if not flag.startswith('GROUP_'):definitions[flag]=0
    # On wired boards WiFi may be removed while keeping network services.
    if flag=='WIFI_SUPPORT' and profile.startswith(('wt32','waveshare')):
     definitions=dict(shared,WIFI_SUPPORT=0)
   key=hashlib.sha256(json.dumps(["explicit-project-rebuild-v3",identity,profile,definitions,a.platform,packages],sort_keys=True).encode()).hexdigest()
   record=root/f'{profile}-{flag}.json';log=root/f'{profile}-{flag}.log'
   item=json.loads(record.read_text()) if record.exists() else None
   if not item or item.get('key')!=key or not item.get('ok'):
    config=ROOT/'build/feature-memory'/f'{profile}.ini';config.parent.mkdir(parents=True,exist_ok=True)
    configuration=configparser.ConfigParser(interpolation=None)
    configuration.read(ROOT/'platformio.ini')
    configuration['platformio']['src_dir']=str(frozen)
    configuration['platformio']['build_dir']=str(ROOT/'build/feature-memory/objects'/profile)
    configuration['platformio']['libdeps_dir']=str(ROOT/'.pio/libdeps')
    if a.platform:
     for section in configuration.sections():
      if section.startswith('env:'):configuration[section]['platform']=a.platform
    # Only project TUs need feature defines. SDK/library flags and caches remain stable.
    override=config.with_suffix('.hpp')
    override.write_text('// Generated comparison overrides.\n'+''.join(f'#define {k} {v}\n' for k,v in sorted(definitions.items())))
    configuration['env:'+profile]['build_src_flags']='-include '+str(override)
    text=io.StringIO();configuration.write(text);config.write_text(text.getvalue())
    # SCons does not reliably track a forced-include file as an implicit
    # dependency on this platform. Recompile project TUs for every uncached
    # variant, retaining all SDK/framework/library objects.
    objects=ROOT/'build/feature-memory/objects'/profile/profile/'src'
    for obj in objects.rglob('*.o'):obj.unlink()
    start=time.monotonic()
    with log.open('w') as out:
     code=subprocess.call([a.pio,'run','-c',str(config),'-e',profile,'-j',str(a.jobs)],cwd=ROOT,stdout=out,stderr=subprocess.STDOUT)
    item=dict(key=key,profile=profile,flag=flag,defines=definitions,ok=code==0,seconds=round(time.monotonic()-start,2),log=str(log))
    build=ROOT/'build/feature-memory/objects'/profile/profile;elf=build/'firmware.elf';binary=build/'firmware.bin'
    if code==0 and elf.exists() and binary.exists():
     ss=sections(elf);slot=app_slot(build/'partitions.bin')
     item.update(binaryBytes=binary.stat().st_size,appSlotBytes=slot,sections=ss,
       dramBytes=sum(v for k,v in ss.items() if re.match(r'\.dram\d+\.(data|bss)$',k)),
       iramBytes=sum(v for k,v in ss.items() if re.match(r'\.iram\d+\.',k)),elfSha256=hashlib.sha256(elf.read_bytes()).hexdigest())
     item['ok']=item['binaryBytes']<=slot
     # Keep evidence without duplicating SDK objects for every feature.
     shutil.copy2(elf,root/f'{profile}-{flag}.elf')
     shutil.copy2(build/'firmware.map',root/f'{profile}-{flag}.map')
    record.write_text(json.dumps(item,indent=2)+'\n')
   if flag=='baseline':baseline=item
   elif item['ok'] and baseline['ok']:
    item['saved']={k:baseline[k]-item[k] for k in ('binaryBytes','dramBytes','iramBytes')}
   results.append(item)
   print(('PASS' if item['ok'] else 'FAIL')+f' {profile} {flag} '+str(item.get('saved',{})),flush=True)
   (root/'results.json').write_text(json.dumps(dict(manifest=manifest,results=results),indent=2)+'\n')
 with (root/'results.csv').open('w') as out:
  w=csv.writer(out);w.writerow(['profile','flag','ok','firmware.bin','DRAM','IRAM','app slot','saved bin','saved DRAM','saved IRAM'])
  for r in results:w.writerow([r['profile'],r['flag'],r['ok'],*[r.get(k,'') for k in ('binaryBytes','dramBytes','iramBytes','appSlotBytes')],*[r.get('saved',{}).get(k,'') for k in ('binaryBytes','dramBytes','iramBytes')]])
 print(root)
 return 0 if all(r['ok'] for r in results) else 1
if __name__=='__main__':raise SystemExit(main())
