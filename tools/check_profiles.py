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

for bt,drive in itertools.product(range(2),range(4)):
    compile_case(['BOARD_PROFILE=10','DISPLAY_KIND=5',f'BLUETOOTH_ENABLED={bt}',f'DRIVE_INTERFACE={drive}'],True)
compile_case(['BOARD_PROFILE=10','DISPLAY_KIND=1'],False)
compile_case(['BOARD_PROFILE=4','DISPLAY_KIND=5'],False)

for board,display,bt,drive in itertools.product((1,3,4),range(3),range(2),range(4)):
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
for display, drive in itertools.product(range(3), range(4)):
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
buttons = ['LOCAL_INPUT_MODE=1', 'LOCAL_AZ_NEG_GPIO=38', 'LOCAL_AZ_POS_GPIO=39',
           'LOCAL_EL_NEG_GPIO=40', 'LOCAL_EL_POS_GPIO=41']
joystick = ['LOCAL_INPUT_MODE=2', 'LOCAL_AZ_ADC_GPIO=1', 'LOCAL_EL_ADC_GPIO=6']
for display, drive in itertools.product(range(3), range(4)):
    compile_case(['BOARD_PROFILE=1', f'DISPLAY_KIND={display}', f'DRIVE_INTERFACE={drive}'] + buttons, True)
    compile_case(['BOARD_PROFILE=1', f'DISPLAY_KIND={display}', f'DRIVE_INTERFACE={drive}'] + joystick, True)
compile_case(['BOARD_PROFILE=4', 'LOCAL_INPUT_MODE=2', 'LOCAL_AZ_ADC_GPIO=36', 'LOCAL_EL_ADC_GPIO=39'], True)
compile_case(['BOARD_PROFILE=3', 'LOCAL_INPUT_MODE=2', 'LOCAL_AZ_ADC_GPIO=1'], True)
compile_case(['BOARD_PROFILE=1', 'LOCAL_INPUT_MODE=1', 'LOCAL_AZ_NEG_GPIO=38', 'LOCAL_AZ_POS_GPIO=39'], True)
for flags in [
    ['LOCAL_INPUT_MODE=-1'], ['LOCAL_INPUT_MODE=3'],
    ['LOCAL_INPUT_MODE=1'], ['LOCAL_INPUT_MODE=2'],
    ['LOCAL_INPUT_MODE=1', 'LOCAL_AZ_NEG_GPIO=38'],
    buttons + ['USER_BUTTON_GPIO=38'],
    ['LOCAL_INPUT_MODE=1', 'LOCAL_AZ_NEG_GPIO=4', 'LOCAL_AZ_POS_GPIO=39'],
    ['LOCAL_INPUT_MODE=1', 'LOCAL_AZ_NEG_GPIO=38', 'LOCAL_AZ_POS_GPIO=38'],
    ['LOCAL_INPUT_MODE=1', 'LOCAL_AZ_NEG_GPIO=-2', 'LOCAL_AZ_POS_GPIO=38'],
    ['BOARD_PROFILE=4', 'LOCAL_INPUT_MODE=1', 'LOCAL_AZ_NEG_GPIO=36', 'LOCAL_AZ_POS_GPIO=39'],
    joystick + ['BLUETOOTH_ENABLED=1'],
    ['LOCAL_INPUT_MODE=2', 'LOCAL_AZ_ADC_GPIO=4'],
    ['LOCAL_INPUT_MODE=2', 'LOCAL_AZ_ADC_GPIO=11'],
    ['LOCAL_INPUT_MODE=2', 'LOCAL_AZ_ADC_GPIO=19'],
    ['LOCAL_INPUT_MODE=2', 'LOCAL_AZ_ADC_GPIO=1', 'LOCAL_EL_ADC_GPIO=1'],
    ['LOCAL_INPUT_MODE=2', 'LOCAL_AZ_ADC_GPIO=1', 'LOCAL_EL_ADC_GPIO=-2'],
    joystick + ['LOCAL_AZ_ADC_CENTER=100'],
    joystick + ['LOCAL_EL_ADC_MIN=2100'],
    joystick + ['LOCAL_EL_ADC_MAX=4096'],
    joystick + ['LOCAL_ADC_DEADZONE=0'],
    joystick + ['LOCAL_ADC_HYSTERESIS=-1'],
    ['LOCAL_AZ_INVERT=2'],
]: compile_case(flags, False)
for drive,bt in itertools.product(range(4),range(2)):
    compile_case(['BOARD_PROFILE=6',f'DRIVE_INTERFACE={drive}',f'BLUETOOTH_ENABLED={bt}','CONFIG_IDF_TARGET_ESP32=1'],True)
compile_case(['ARDUINO_WT32_ETH01','CONFIG_IDF_TARGET_ESP32=1'],True)
compile_case(['BOARD_PROFILE=6','BLUETOOTH_ENABLED=1','BLUETOOTH_MODE=BT_HC05'],True)
compile_case(['BOARD_PROFILE=6','LOCAL_INPUT_MODE=2','LOCAL_AZ_ADC_GPIO=39'],True)
for flags in [
    ['BOARD_PROFILE=6','DISPLAY_KIND=1'], ['BOARD_PROFILE=6','DISPLAY_KIND=2'],
    ['BOARD_PROFILE=6','ARDUINO_USB_CDC_ON_BOOT=1'],
    ['BOARD_PROFILE=6','CONFIG_IDF_TARGET_ESP32S3=1'],
    *[['BOARD_PROFILE=6',f'USER_BUTTON_GPIO={pin}'] for pin in (0,16,18,19,21,22,23,25,26,27,34,13)],
    ['BOARD_PROFILE=6','LOCAL_INPUT_MODE=2','LOCAL_AZ_ADC_GPIO=25'],
]: compile_case(flags,False)
panel=['BOARD_PROFILE=8','DISPLAY_KIND=4']
compile_case(panel+['CONFIG_IDF_TARGET_ESP32C3=1'],True)
compile_case(['CONFIG_IDF_TARGET_ESP32C3=1'],True)
compile_case(panel+['DRIVE_SIMULATION=1'],True)
for flags in [['BLUETOOTH_ENABLED=1'],['DRIVE_INTERFACE=1'],['DRIVE_INTERFACE=2'],['USER_BUTTON_GPIO=8'],
              ['USER_BUTTON_GPIO=-1','LOCAL_INPUT_MODE=1','LOCAL_AZ_NEG_GPIO=8','LOCAL_AZ_POS_GPIO=9']]:
    compile_case(panel+flags,True)
for flags in [
    panel+['CONFIG_IDF_TARGET_ESP32=1'],panel+['CONFIG_IDF_TARGET_ESP32S3=1'],
    panel+['CONFIG_IDF_TARGET_ESP32S2=1'],['CONFIG_IDF_TARGET_ESP32C3=1','DISPLAY_KIND=4'],
    ['BOARD_PROFILE=1','CONFIG_IDF_TARGET_ESP32C3=1'],['BOARD_PROFILE=10'],
    ['BOARD_PROFILE=1','DISPLAY_KIND=4'],panel+['USER_BUTTON_GPIO=7'],
    panel+['USER_BUTTON_GPIO=18'],panel+['USER_BUTTON_GPIO=12'],
    panel+['LOCAL_INPUT_MODE=1','LOCAL_AZ_NEG_GPIO=8','LOCAL_AZ_POS_GPIO=9'],
]: compile_case(flags,False)
headless=['BOARD_PROFILE=9','DISPLAY_KIND=0']
for drive,bt,simulation in itertools.product(range(4),range(2),range(2)):
    compile_case(headless+[f'DRIVE_INTERFACE={drive}',f'BLUETOOTH_ENABLED={bt}',
                          f'DRIVE_SIMULATION={simulation}','CONFIG_IDF_TARGET_ESP32C3=1'],True)
for pin in range(9):
    compile_case(headless+[f'USER_BUTTON_GPIO={pin}'],True)
for flags in [
    *[['BOARD_PROFILE=9',f'DISPLAY_KIND={display}'] for display in range(1,5)],
    *[headless+[f'CONFIG_IDF_TARGET_{chip}=1'] for chip in ('ESP32','ESP32S2','ESP32S3')],
    headless+['BLUETOOTH_ENABLED=1','BLUETOOTH_MODE=BT_ESP32_SPP'],
    *[headless+[f'USER_BUTTON_GPIO={pin}'] for pin in range(11,20)],
]: compile_case(flags,False)

# External QSPI module is independent of the ESP32 / ESP32-S3 board profile.
for board in (1,4):
    compile_case([f'BOARD_PROFILE={board}','DISPLAY_KIND=6'],True)
    compile_case([f'BOARD_PROFILE={board}','DISPLAY_KIND=6','ST77916_SPI_HZ=0'],False)
    compile_case([f'BOARD_PROFILE={board}','DISPLAY_KIND=6','ST77916_INIT_VARIANT=3'],False)
for board in (2,3,5,6,7,8,9,10):
    compile_case([f'BOARD_PROFILE={board}','DISPLAY_KIND=6'],False)

print(f'PASS: {checks} board/display/BT/drive/local-input ownership and negative compile checks')
