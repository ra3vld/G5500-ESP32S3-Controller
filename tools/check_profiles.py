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

for board,display,bt,drive in itertools.product((1,3),range(3),range(2),range(3)):
    compile_case([f'BOARD_PROFILE={board}',f'DISPLAY_KIND={display}',f'BLUETOOTH_ENABLED={bt}',f'DRIVE_INTERFACE={drive}'],True)
for flags in [
    ['BOARD_PROFILE=1','CONFIG_IDF_TARGET_ESP32S2=1'],
    ['BOARD_PROFILE=3','CONFIG_IDF_TARGET_ESP32S3=1'],
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
print(f'PASS: {checks} board/display/BT/drive ownership and negative compile checks')
