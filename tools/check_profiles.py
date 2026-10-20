#!/usr/bin/env python3
"""Compile pin ownership/capability checks, including deliberately invalid profiles."""
import itertools
import os
import pathlib
import subprocess
ROOT = pathlib.Path(__file__).resolve().parents[1]
source = '#include "board.hpp"\nint main(){}\n'
checks=0

def compile_case(flags, expected):
    global checks
    result=subprocess.run([os.environ.get('CXX','g++'),'-std=c++17','-fsyntax-only','-x','c++','-I',str(ROOT/'G5500_Fork'),*['-D'+x for x in flags],'-'],input=source,text=True,capture_output=True)
    if (result.returncode==0) != expected:
        raise SystemExit(f'Unexpected result for {flags}:\n{result.stderr}')
    checks+=1

for board,display,bt,drive in itertools.product((1,3,4),range(3),range(2),range(3)):
    compile_case([f'BOARD_PROFILE={board}',f'DISPLAY_KIND={display}',f'BLUETOOTH_ENABLED={bt}',f'DRIVE_INTERFACE={drive}'],True)
for flags in [
    ['BOARD_PROFILE=UNKNOWN_PROFILE'],
    ['BOARD_PROFILE=1','CONFIG_IDF_TARGET_ESP32S2=1'],
    ['BOARD_PROFILE=3','CONFIG_IDF_TARGET_ESP32S3=1'],
    ['BOARD_PROFILE=4','CONFIG_IDF_TARGET_ESP32S2=1'],
    ['BOARD_PROFILE=1','CONFIG_IDF_TARGET_ESP32=1'],
    ['BOARD_PROFILE=4','ARDUINO_USB_CDC_ON_BOOT=1'],
    *[[f'BOARD_PROFILE=4',f'USER_BUTTON_GPIO={pin}'] for pin in (6,11,16,17,20,24,28,37,38,39)],
    ['BOARD_PROFILE=2','DRIVE_INTERFACE=1'],
    ['BOARD_PROFILE=2','DRIVE_INTERFACE=2'],
    ['USER_BUTTON_GPIO=4'], ['USER_BUTTON_GPIO=21'], ['USER_BUTTON_GPIO=48'],
    ['USER_BUTTON_GPIO=19'], ['USER_BUTTON_GPIO=35'], ['USER_BUTTON_GPIO=49'],
    ['USER_BUTTON_GPIO=-2'], ['BLUETOOTH_ENABLED=2'],
    ['USER_BUTTON_GPIO=8','DISPLAY_KIND=2'],
    ['USER_BUTTON_GPIO=6','BLUETOOTH_ENABLED=1'],
    ['DISPLAY_KIND=2','TFT_DISPLAY_ENABLED=1'], ['DISPLAY_KIND=3'],
    ['DRIVE_INTERFACE=2','SOC_LEDC_CHANNEL_NUM=2'], ['BOARD_PROFILE=3','USER_BUTTON_GPIO=44'],
]:compile_case(flags,False)
for flags in [['USER_BUTTON_GPIO=-1'],['USER_BUTTON_GPIO=38'],['USER_BUTTON_GPIO=6','BLUETOOTH_ENABLED=0'],['USER_BUTTON_GPIO=8','DISPLAY_KIND=0'],['BOARD_PROFILE=3','CONFIG_IDF_TARGET_ESP32S2=1'],['TFT_DISPLAY_ENABLED=0'],['TFT_DISPLAY_ENABLED=1'],['USER_BUTTON_GPIO=47','DRIVE_INTERFACE=0']]:compile_case(flags,True)
for flags in [['CONFIG_IDF_TARGET_ESP32=1'],['BOARD_PROFILE=4','USER_BUTTON_GPIO=39','USER_BUTTON_PULLUP=0'],['BOARD_PROFILE=4','USER_BUTTON_GPIO=-1']]:compile_case(flags,True)
crow = ['BOARD_PROFILE=5', 'DISPLAY_KIND=3', 'USER_BUTTON_GPIO=-1']
for bt in (0, 1):
    compile_case(crow + [f'BLUETOOTH_ENABLED={bt}', 'CONFIG_IDF_TARGET_ESP32S3=1'], True)
for flag in ('DRIVE_INTERFACE=1', 'DRIVE_INTERFACE=2', 'CONFIG_IDF_TARGET_ESP32=1', 'CONFIG_IDF_TARGET_ESP32S2=1'):
    compile_case(crow + [flag], False)
compile_case(['BOARD_PROFILE=5', 'DISPLAY_KIND=3'], True)
compile_case(['BOARD_PROFILE=5', 'USER_BUTTON_GPIO=-1'], False)
for drive in (0, 2):
    compile_case(crow + ['CROWPANEL_LOCAL_DRIVE=1', f'DRIVE_INTERFACE={drive}', 'SOC_LEDC_CHANNEL_NUM=2'], True)
for flags in [
    ['CROWPANEL_LOCAL_DRIVE=2'], ['CROWPANEL_LOCAL_DRIVE=1'],
    crow + ['CROWPANEL_LOCAL_DRIVE=1', 'BLUETOOTH_ENABLED=1'],
    crow + ['CROWPANEL_LOCAL_DRIVE=1', 'DRIVE_INTERFACE=1'],
    crow + ['CROWPANEL_LOCAL_DRIVE=1', 'SOC_LEDC_CHANNEL_NUM=1'],
]: compile_case(flags, False)
for display, drive in itertools.product(range(3), range(3)):
    compile_case(['BOARD_PROFILE=4', f'DISPLAY_KIND={display}', f'DRIVE_INTERFACE={drive}', 'BLUETOOTH_ENABLED=1', 'BLUETOOTH_MODE=BT_HC05'], True)
for flags in [
    ['BLUETOOTH_ENABLED=1', 'BLUETOOTH_MODE=BT_ESP32_SPP'],
    ['BLUETOOTH_MODE=4'], ['BLUETOOTH_MODE=0'], ['DRIVE_SIMULATION=2'],
    ['BOARD_PROFILE=4', 'BLUETOOTH_ENABLED=1', 'BLUETOOTH_MODE=BT_HC05', 'USER_BUTTON_GPIO=5'],
]: compile_case(flags, False)
for board in range(1, 6):
    for simulation in (0, 1):
        compile_case((crow if board == 5 else [f'BOARD_PROFILE={board}']) + [f'DRIVE_SIMULATION={simulation}'], True)
compile_case(['BOARD_PROFILE=1','USER_BUTTON_GPIO=46'],True)
compile_case(['BOARD_PROFILE=2','USER_BUTTON_GPIO=46'],False)
compile_case(['BOARD_PROFILE=2','USER_BUTTON_GPIO=46','USER_BUTTON_PULLUP=0'],True)
print(f'PASS: {checks} board/display/BT/drive ownership and negative compile checks')
