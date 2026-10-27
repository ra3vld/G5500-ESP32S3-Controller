#!/usr/bin/env python3
"""Compare identical firmware snapshots with individual compile-time capabilities removed.

Results and logs stay in ignored local-notes/. Reuses each profile's PlatformIO
objects and immutable JSON results; never uploads. --platform supports a local
copy of the pinned platform for offline builds.
"""
import argparse
import csv
import fcntl
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
 'ETHERNET_SUPPORT':{},'WEB_INTERFACE_ENABLED':{'WEB_LIVE_SUPPORT':0,'CAMERA_ENABLED':0},'WEB_LIVE_SUPPORT':{},'WEB_PLAIN_HTML_SUPPORT':{},
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

def digest(paths,base=ROOT):
 h=hashlib.sha256()
 for p in sorted(paths):h.update(str(p.relative_to(base)).encode()+b'\0'+p.read_bytes())
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

def option(config, section, key):
 value=config.get(section,key,fallback=None)
 if value is None:
  parent=config.get(section,'extends',fallback=None)
  value=option(config,parent,key) if parent else ''
 return re.sub(r'\$\{([^}]+)\}',lambda m:option(config,*m.group(1).rsplit('.',1)),value)

def offline_libraries(config, profile, pool, definitions):
 if definitions.get('DISPLAY_SUPPORT')==0:return []
 dependencies=option(config,'env:'+profile,'lib_deps')
 names=[('XPT2046_Touchscreen' if 'github.com/PaulStoffregen/XPT2046_Touchscreen/' in line else
         line.split('@',1)[0].strip().split('/')[-1]) for line in dependencies.splitlines() if line.strip()]
 normalize=lambda s:re.sub('[^a-z0-9]','',s.lower())
 found={}
 for folder in pool.iterdir():
  if not folder.is_dir():continue
  meta=folder/'library.json';properties=folder/'library.properties'
  name=json.loads(meta.read_text()).get('name',folder.name) if meta.exists() else folder.name
  if properties.exists():
   match=re.search(r'^name=(.+)$',properties.read_text(),re.M)
   if match:name=match.group(1)
  found[normalize(name)]=folder
 paths=[]
 for name in names:
  if normalize(name) not in found:raise ValueError('Pinned offline library missing: '+name)
  paths.append(found[normalize(name)])
 # PlatformIO resolves a package's dependencies immediately while installing it.
 # Install the pinned prerequisites first to avoid an unpinned registry fetch.
 priorities={'adafruitbusio':0,'adafruitgfxlibrary':1}
 paths.sort(key=lambda path:priorities.get(normalize(path.name),2))
 return paths

def dependency_closure_available(libraries):
 normalize=lambda s:re.sub('[^a-z0-9]','',s.lower())
 names={normalize(path.name) for path in libraries}
 for library in libraries:
  properties=library/'library.properties'
  if properties.exists():
   match=re.search(r'^depends=(.+)$',properties.read_text(),re.M)
   if match:
    dependencies=[normalize(re.sub(r'\([^)]*\)','',name)) for name in match.group(1).split(',')]
    if any(name not in names for name in dependencies):return False
 return True

def summarize(root):
 manifest=json.loads((root/'manifest.json').read_text());records=[]
 for path in sorted(root.glob('*.json')):
  item=json.loads(path.read_text())
  if isinstance(item,dict) and item.get('measurementVersion')==8 and 'profile' in item and 'flag' in item:records.append(item)
 baselines={r['profile']:r for r in records if r['flag']=='baseline'}
 for item in records:
  baseline=baselines.get(item['profile'])
  if item['flag']!='baseline' and item['ok'] and baseline and baseline['ok']:
   item['saved']={k:baseline[k]-item[k] for k in ('binaryBytes','dramBytes','iramBytes')}
   absent={'esp32_3248s035c':{'ETHERNET_SUPPORT','CAMERA_ENABLED','WEB_PLAIN_HTML_SUPPORT'},
           'esp32s3_n16r8':{'DISPLAY_SUPPORT','ETHERNET_SUPPORT','CAMERA_ENABLED','BLUETOOTH_SPP_SUPPORT'}}
   if item['flag'] in absent.get(item['profile'],set()) and any(item['saved'].values()):
    raise ValueError(f"Inactive feature changed size: {item['profile']} {item['flag']}; verify paired libraries/toolchain/configuration")
 (root/'results.json').write_text(json.dumps(dict(manifest=manifest,results=records),indent=2)+'\n')
 with (root/'results.csv').open('w') as out:
  w=csv.writer(out);w.writerow(['profile','flag','ok','firmware.bin','DRAM','IRAM','app slot','saved bin','saved DRAM','saved IRAM'])
  for r in records:w.writerow([r['profile'],r['flag'],r['ok'],*[r.get(k,'') for k in ('binaryBytes','dramBytes','iramBytes','appSlotBytes')],*[r.get('saved',{}).get(k,'') for k in ('binaryBytes','dramBytes','iramBytes')]])
 lines=['# Сравнение функций прошивки','',f"Исходники: `{manifest['revision']}`, SHA-256 `{manifest['sourceSha256']}`.",
  '', 'Числа в байтах. Экономия — базовая сборка минус сборка с выключенным флагом. Не складывать пересекающиеся зависимости.',
  '', '| Профиль | Флаг | Результат | BIN | DRAM | IRAM | Раздел | Экономия BIN / DRAM / IRAM |','|---|---|---|---:|---:|---:|---:|---|']
 for r in records:
  saved=r.get('saved',{});delta=' / '.join(str(saved[k]) for k in ('binaryBytes','dramBytes','iramBytes')) if saved else '—'
  lines.append('| '+' | '.join([r['profile'],r['flag'],'PASS' if r['ok'] else 'FAIL',*[str(r.get(k,'—')) for k in ('binaryBytes','dramBytes','iramBytes','appSlotBytes')],delta])+' |')
 (root/'results.md').write_text('\n'.join(lines)+'\n')
 return records

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--profiles',nargs='+',default=['esp32_3248s035c','esp32s3_n16r8'])
 p.add_argument('--flags',nargs='+',choices=['baseline',*FLAGS],default=list(FLAGS))
 p.add_argument('--pio',default=os.environ.get('PIO','pio'));p.add_argument('--platform')
 p.add_argument('--jobs',type=int,default=4);p.add_argument('--library-dir',type=Path,help='Verified pinned offline libraries')
 p.add_argument('--ccache',type=Path,help='Optional compiler cache executable')
 p.add_argument('--output',type=Path,default=ROOT/'local-notes/feature-memory')
 p.add_argument('--snapshot',type=Path,help='Resume an existing immutable snapshot directory')
 p.add_argument('--rebuild',action='store_true',help='Ignore cached measurements for the requested comparisons')
 p.add_argument('--report-only',action='store_true',help='Merge completed snapshot records without building')
 p.add_argument('--defines',nargs='*',default=[],help='Shared baseline overrides NAME=VALUE')
 a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True)
 if not a.output.is_relative_to(ROOT/'local-notes'):p.error('Keep measurements in local-notes/')
 if a.report_only:
  if not a.snapshot:p.error('--report-only requires --snapshot')
  root=a.snapshot.resolve()
  if not root.is_relative_to(a.output):p.error('Snapshot must belong to --output')
  records=summarize(root);print(f'{root}: {sum(r["ok"] for r in records)}/{len(records)} successful records');return 0
 for name in a.profiles:
  if not re.fullmatch('[a-zA-Z0-9_]+',name):p.error('Invalid profile')
 shared={}
 for definition in a.defines:
  if not re.fullmatch('[A-Z_0-9]+=[01]',definition):p.error('Invalid override')
  name,value=definition.split('=');shared[name]=int(value)
 previous=None
 if a.snapshot:
  root=a.snapshot.resolve()
  if not root.is_relative_to(a.output):p.error('Snapshot must belong to --output')
  previous=json.loads((root/'manifest.json').read_text())
 source=(ROOT/'build/feature-memory/sources'/previous['sourceSha256']/'G5500_Fork') if previous else ROOT/'G5500_Fork'
 paths=[x for x in source.rglob('*') if x.is_file() and x.suffix in {'.ino','.cpp','.hpp','.h','.c','.csv'} and not x.name.endswith('.ino.cpp')]
 if previous and (not paths or digest(paths,source.parent)!=previous['sourceSha256']):
  p.error('Frozen snapshot is missing or modified; do not mix measurements from different sources')
 source_hash=previous['sourceSha256'] if previous else digest(paths)
 identity=previous['identity'] if previous else digest([*paths,ROOT/'platformio.ini'])
 root=a.output/identity[:16];root.mkdir(exist_ok=True)
 frozen=ROOT/'build/feature-memory/sources'/source_hash/'G5500_Fork'
 frozen.mkdir(parents=True,exist_ok=True)
 if not previous:
  for f in paths:
   out=frozen/f.relative_to(source);out.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(f,out)
 version=subprocess.check_output([a.pio,'--version'],text=True).strip()
 packages=[]
 core=Path(os.environ.get('PLATFORMIO_CORE_DIR',str(Path.home()/'.platformio')))
 for f in sorted((core/'packages').glob('*/package.json')):
  meta=json.loads(f.read_text());packages.append(dict(name=meta.get('name'),version=meta.get('version')))
 manifest=dict(sourceSha256=source_hash,identity=identity,revision=previous['revision'] if previous else subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
  utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),platformio=version,platform=a.platform,packages=packages,sharedDefines=shared)
 if not previous:(root/'platformio.snapshot.ini').write_bytes((ROOT/'platformio.ini').read_bytes())
 (root/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
 library_fingerprint=[]
 if a.library_dir:
  for library in sorted(a.library_dir.resolve().iterdir()):
   if not library.is_dir():continue
   h=hashlib.sha256()
   for file in sorted(library.rglob('*')):
    if file.is_file() and (file.suffix in {'.h','.hpp','.c','.cpp','.S'} or file.name in {'library.json','library.properties'}):
     h.update(str(file.relative_to(library)).encode()+b'\0'+file.read_bytes())
   library_fingerprint.append([library.name,h.hexdigest()])
 results=[]
 for profile in a.profiles:
  lock=(a.output/(profile+'.lock')).open('a')
  fcntl.flock(lock,fcntl.LOCK_EX)
  profile_source=frozen.parent/profile/'G5500_Fork';profile_source.mkdir(parents=True,exist_ok=True)
  for f in paths:
   destination=profile_source/f.relative_to(source);destination.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(frozen/f.relative_to(source),destination)
  baseline=None
  for flag in dict.fromkeys(['baseline',*a.flags]):
   definitions=dict(shared)
   if flag!='baseline':
    definitions.update(FLAGS[flag])
    if not flag.startswith('GROUP_'):definitions[flag]=0
    # On wired boards WiFi may be removed while keeping network services.
    if flag=='WIFI_SUPPORT' and profile.startswith(('wt32','waveshare')):
     definitions=dict(shared,WIFI_SUPPORT=0)
   config=ROOT/'build/feature-memory'/f'{profile}.ini';config.parent.mkdir(parents=True,exist_ok=True)
   configuration=configparser.ConfigParser(interpolation=None)
   configuration.read(root/'platformio.snapshot.ini')
   configuration['platformio']['src_dir']=str(profile_source)
   configuration['env']['extra_scripts']='' # The frozen identity belongs to this snapshot.
   if a.ccache:
    cache_script=config.with_suffix('.cache.py')
    cache_script.write_text('Import("env")\nimport os\n'
     'targets=[env,env.ConfigureProjectLibBuilder().env]+[b.env for b in env.GetLibBuilders()]\n'
     'for current in targets:\n'
     ' for key in ("CCACHE_DIR","CCACHE_BASEDIR","LD_LIBRARY_PATH"):\n'
     '  if key in os.environ:current["ENV"][key]=os.environ[key]\n'
     ' for compiler in ("CC","CXX"):\n'
     '  if "ccache" not in current[compiler]:current[compiler]='+repr(str(a.ccache.resolve())+' ')+'+current[compiler]\n')
    # Profiles with their own scripts need the cache script appended there.
    scripts=option(configuration,'env:'+profile,'extra_scripts')
    configuration['env:'+profile]['extra_scripts']=scripts+'\npost:'+str(cache_script)
   if a.library_dir:
    libraries=offline_libraries(configuration,profile,a.library_dir.resolve(),definitions)
    pool=ROOT/'build/feature-memory/library-pools'/profile;pool.mkdir(parents=True,exist_ok=True)
    for link in pool.iterdir():
     if link.is_symlink():link.unlink()
    for library in libraries:(pool/library.name).symlink_to(library,target_is_directory=True)
    # Some manifests declare unused touch controllers (ILI9341). Discover
    # headers from the pinned pool instead of fetching their optional extras.
    configuration['env:'+profile]['lib_deps']='\n'.join('symlink://'+str(library) for library in libraries) if dependency_closure_available(libraries) else ''
    configuration['env:'+profile]['lib_extra_dirs']=str(pool)
   configuration['platformio']['build_dir']=str(ROOT/'build/feature-memory/objects'/profile)
   configuration['platformio']['libdeps_dir']=str(ROOT/'build/feature-memory/libdeps')
   if a.platform:
    for section in configuration.sections():
     if section.startswith('env:'):configuration[section]['platform']=a.platform
   # Only project TUs need feature defines. SDK/library flags and caches remain stable.
   override=config.with_suffix('.hpp')
   override.write_text('// Generated comparison overrides.\n'+''.join(f'#define {k} {v}\n' for k,v in sorted(definitions.items())))
   configuration['env:'+profile]['build_src_flags']='-include '+str(override)
   text=io.StringIO();configuration.write(text);config.write_text(text.getvalue())
   inputs=["paired-config-v8",identity,profile,definitions,packages,version,text.getvalue(),override.read_text()]
   for script in ('platformio_lto.py','platformio_image_size.py','check_firmware_size.py'):
    inputs.append(hashlib.sha256((ROOT/'tools'/script).read_bytes()).hexdigest())
   if a.library_dir:inputs.append(library_fingerprint)
   key=hashlib.sha256(json.dumps(inputs,sort_keys=True).encode()).hexdigest()
   record=root/f'{profile}-{flag}.json';log=root/f'{profile}-{flag}.log'
   item=json.loads(record.read_text()) if record.exists() else None
   if a.rebuild or not item or item.get('key')!=key or not item.get('ok'):
    # SCons does not reliably track a forced-include file as an implicit
    # dependency on this platform. Recompile project TUs for every uncached
    # variant, retaining all SDK/framework/library objects.
    objects=ROOT/'build/feature-memory/objects'/profile/profile/'src'
    for obj in objects.rglob('*.o'):obj.unlink()
    start=time.monotonic()
    with log.open('w') as out:
     code=subprocess.call([a.pio,'run','--disable-auto-clean','-c',str(config),'-e',profile,'-j',str(a.jobs)],cwd=ROOT,stdout=out,stderr=subprocess.STDOUT)
    item=dict(measurementVersion=8,configuration=text.getvalue(),overrideHeader=override.read_text(),key=key,profile=profile,flag=flag,defines=definitions,ok=code==0,seconds=round(time.monotonic()-start,2),log=str(log))
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
  fcntl.flock(lock,fcntl.LOCK_UN);lock.close()
 with (root/'results.csv').open('w') as out:
  w=csv.writer(out);w.writerow(['profile','flag','ok','firmware.bin','DRAM','IRAM','app slot','saved bin','saved DRAM','saved IRAM'])
  for r in results:w.writerow([r['profile'],r['flag'],r['ok'],*[r.get(k,'') for k in ('binaryBytes','dramBytes','iramBytes','appSlotBytes')],*[r.get('saved',{}).get(k,'') for k in ('binaryBytes','dramBytes','iramBytes')]])
 print(root)
 return 0 if all(r['ok'] for r in results) else 1
if __name__=='__main__':raise SystemExit(main())
