#!/usr/bin/env python3
"""Select optional PlatformIO dependencies from build_config.hpp and build flags.
This only builds firmware; it never uploads. Direct pio *_tft/*_lcd environments
are also available when an explicit display environment is preferred.
"""
import argparse
import configparser
import os
from pathlib import Path
import re
import shlex
import subprocess
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--env',default='esp32s3_n16r8')
p.add_argument('--display',choices=['none','tft','lcd','crowpanel','gc9a01','st7796'])
p.add_argument('--bluetooth',choices=['0','1'])
p.add_argument('--bluetooth-mode',choices=['auto','hc05','spp'])
p.add_argument('--simulation',choices=['0','1'],help='Virtual drive on any board; disables motor GPIO/PWM and feedback ADC')
p.add_argument('--drive',choices=['yaesu','separate-pwm','bts7960','dir-pwm'])
p.add_argument('--pwm-inverted',choices=['0','1'],help='Invert only BTS7960 RPWM/LPWM for optocoupler wiring')
p.add_argument('--pio',default=os.environ.get('PIO','pio'))
p.add_argument('--jobs',type=int,default=4)
p.add_argument('--motion-hz',type=int,choices=[50,100,200],help='Control/feedback rate; 100/200 Hz require hardware timing and noise checks')
p.add_argument('--dry-run',action='store_true')
a=p.parse_args()
config=configparser.ConfigParser(interpolation=None);config.read(ROOT/'platformio.ini')
section='env:'+a.env
if section not in config:p.error('Unknown environment '+a.env)
def option(section,key):
    value=config.get(section,key,fallback=None)
    if value is None:
        parent=config.get(section,'extends',fallback=None)
        value=option(parent,key) if parent else ''
    def expand(match):
        group=match.group(1);parent,key=group.rsplit('.',1)
        return option(parent,key)
    return re.sub(r'\$\{([^}]+)\}',expand,value)
raw_flags=shlex.split(option(section,'build_flags'))
flags=[]
index=0
while index<len(raw_flags):
    flag=raw_flags[index]
    if flag=='-D' and index+1<len(raw_flags):
        index+=1;flag+=''+raw_flags[index]
    flags.append(flag);index+=1
macros={}
for flag in flags:
    if flag.startswith('-D'):
        key,_,value=flag[2:].partition('=');macros[key]=value or '1'
header=(ROOT/'G5500_Fork/build_config.hpp').read_text()
# Allow users to indent/align settings without breaking dependency selection.
def defaults(name):
    return re.findall(r'^\s*#\s*define\s+'+re.escape(name)+r'\s+(\w+)',header,re.M)
# A user-edited DISPLAY_KIND wins unless a build flag overrides it.
values=defaults('DISPLAY_KIND')
display=macros.get('DISPLAY_KIND')
if display is None:
    if 'TFT_DISPLAY_ENABLED' in macros:display=macros['TFT_DISPLAY_ENABLED']
    else:display=next((v for v in values if v!='TFT_DISPLAY_ENABLED'),'DISPLAY_NONE')
kind={'DISPLAY_NONE':0,'DISPLAY_ILI9341':1,'DISPLAY_LCD1602':2,'DISPLAY_CROWPANEL_21':3,'DISPLAY_GC9A01':4,'DISPLAY_ST7796':5,'0':0,'1':1,'2':2,'3':3,'4':4,'5':5}.get(display)
if a.display:kind={'none':0,'tft':1,'lcd':2,'crowpanel':3,'gc9a01':4,'st7796':5}[a.display]
if kind is None:p.error('Cannot resolve DISPLAY_KIND; use --display')
# Replace display flags atomically, retaining the legacy alias only when consistent.
flags=[f for f in flags if not f.startswith(('-DDISPLAY_KIND=','-DTFT_DISPLAY_ENABLED='))]
flags += ['-DDISPLAY_KIND='+str(kind),'-DTFT_DISPLAY_ENABLED='+str(int(kind==1))]
if a.bluetooth is not None:
    flags=[f for f in flags if not f.startswith('-DBLUETOOTH_ENABLED=')];flags.append('-DBLUETOOTH_ENABLED='+a.bluetooth)
if a.bluetooth_mode is not None:
    flags=[f for f in flags if not f.startswith('-DBLUETOOTH_MODE=')]
    flags.append('-DBLUETOOTH_MODE='+{'auto':'BT_AUTO','hc05':'BT_HC05','spp':'BT_ESP32_SPP'}[a.bluetooth_mode])
if a.simulation is not None:
    flags=[f for f in flags if not f.startswith('-DDRIVE_SIMULATION=')]
    flags.append('-DDRIVE_SIMULATION='+a.simulation)
if a.drive:
    flags=[f for f in flags if not f.startswith('-DDRIVE_INTERFACE=')]
    flags.append('-DDRIVE_INTERFACE='+str({'yaesu':0,'separate-pwm':1,'bts7960':2,'dir-pwm':3}[a.drive]))
if a.pwm_inverted is not None:
    flags=[f for f in flags if not f.startswith('-DBTS7960_PWM_INVERTED=')]
    flags.append('-DBTS7960_PWM_INVERTED='+a.pwm_inverted)
if a.motion_hz is not None:
    flags=[f for f in flags if not f.startswith('-DMOTION_CONTROL_PERIOD_MS=')]
    flags.append('-DMOTION_CONTROL_PERIOD_MS='+str(1000//a.motion_hz))
deps=option('env:esp32_3248s035c','lib_deps') if kind==5 else option('env:esp32_2424s012c','lib_deps') if kind==4 else '' if kind==0 else 'lvgl/lvgl @ 9.6.0' if kind==3 else 'arduino-libraries/LiquidCrystal @ 1.0.7' if kind==2 else option('display_tft','lib_deps')
if kind in (1,3,4,5):
    flags += ["-DLV_CONF_INCLUDE_SIMPLE", "-I"+str(ROOT/"G5500_Fork")]
# The 3 MiB classic-ESP32 application also needs LTO with the shared LVGL UI.
lto_override = kind == 1 and macros.get('BOARD_PROFILE') in ('BOARD_ESP32', '4')
if lto_override and '-flto' not in flags:
    flags.append('-flto')
# Classic SPP uses IDF directly; no additional Bluetooth library is needed.
work=ROOT/'build/platformio-config';work.mkdir(parents=True,exist_ok=True)
path=work/(a.env+'.ini')
scripts = option(section, 'extra_scripts').split()
if 'pre:tools/platformio_version.py' not in scripts:
    scripts.insert(0, 'pre:tools/platformio_version.py')
if lto_override and 'post:tools/platformio_lto.py' not in scripts:
    scripts.append('post:tools/platformio_lto.py')
if kind in (1, 3, 4, 5) and 'post:tools/platformio_image_size.py' not in scripts:
    scripts.append('post:tools/platformio_image_size.py')
script_config = '\nextra_scripts = ' + ' '.join(scripts) if scripts else ''
path.write_text(f'[platformio]\nextra_configs = {ROOT}/platformio.ini\nsrc_dir = {ROOT}/G5500_Fork\n[env:selected]\nextends = {section}\nbuild_flags = '+ ' '.join(flags)+script_config+('\nlib_ldf_mode = chain' if kind in (1,3,4,5) else '')+'\nlib_deps =\n'+''.join(' '+line+'\n' for line in deps.splitlines() if line.strip()))
cmd=[a.pio,'run','-c',str(path),'-e','selected','-j',str(a.jobs)]
print(shlex.join(cmd),flush=True)
if not a.dry_run:raise SystemExit(subprocess.call(cmd,cwd=ROOT))
