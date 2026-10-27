"""Exercise edited user settings, build overrides and dependency selection."""
import configparser
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BuildConfigTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / 'G5500_Fork'
        self.source.mkdir()
        for path in (ROOT / 'G5500_Fork').iterdir():
            if path.suffix in {'.hpp', '.h', '.c', '.cpp', '.ino', '.csv'} and not path.name.endswith('.ino.cpp'):
                shutil.copy2(path, self.source / path.name)
        shutil.copytree(ROOT / 'G5500_Fork/src', self.source / 'src')
        (self.root / 'tools').mkdir()
        for name in ['build_platformio.py', 'prepare_sketch.py', 'generate_web_assets.py', 'project_version.py', 'web_poll.js']:
            shutil.copy2(ROOT / 'tools' / name, self.root / 'tools' / name)
        shutil.copy2(ROOT / 'platformio.ini', self.root / 'platformio.ini')

    def edit(self, **settings):
        for name, value in settings.items():
            path = self.source / ('build_config.hpp' if name in {'BOARD_PROFILE', 'DISPLAY_KIND', 'DISPLAY_SUPPORT'} else 'runtime_defaults.hpp')
            text = path.read_text()
            # Realistic formatting: indentation, spaces after #, aligned values.
            text, count = re.subn(r'^#define\s+' + name + r'\s+[^\n]+',
                                 '  # define ' + name + '    ' + str(value), text, flags=re.M)
            self.assertEqual(count, 1, name)
            path.write_text(text)

    def compile(self, expected, flags=(), success=True):
        assertions = '\n'.join(f'static_assert({name} == {value}, "{name}");'
                               for name, value in expected.items())
        result = subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++17', '-fsyntax-only',
                                 '-I', str(self.source), *['-D' + flag for flag in flags],
                                 '-x', 'c++', '-'],
                                input='#include "board.hpp"\n' + assertions, text=True, capture_output=True)
        self.assertEqual(result.returncode == 0, success, result.stderr)

    def test_feature_dependencies(self):
        flags=['DISPLAY_SUPPORT','LOCAL_INPUT_SUPPORT','WIFI_SUPPORT','ETHERNET_SUPPORT','WEB_LIVE_SUPPORT','WEB_PLAIN_HTML_SUPPORT',
               'BLUETOOTH_SPP_SUPPORT','GS232_SUPPORT','ROTCTLD_SUPPORT','N1MM_SUPPORT','PSTROTATOR_SUPPORT',
               'COMMAND_API_SUPPORT','HUD_SUPPORT','HUD_UDP_SUPPORT','HUD_UART_SUPPORT','RS485_SUPPORT',
               'MODBUS_RTU_SUPPORT','OTA_SUPPORT','ARDUINO_OTA_SUPPORT','LOG_HISTORY_SUPPORT',
               'RSSI_HISTORY_SUPPORT','RESET_HISTORY_SUPPORT','NTP_SUPPORT','MQTT_SUPPORT','HOME_ASSISTANT_DISCOVERY_SUPPORT']
        for name in flags:
            with self.subTest(flag=name):
                self.compile({},[name+'=0']+(['WEB_INTERFACE_ENABLED=0'] if name=='WIFI_SUPPORT' else []))
                self.compile({},[name+'=2'],success=False)
        for definitions in [
            ['WIFI_SUPPORT=0','MQTT_SUPPORT=1'],
            ['MQTT_SUPPORT=0','HOME_ASSISTANT_DISCOVERY_SUPPORT=1'],
            ['RS485_SUPPORT=0','MODBUS_RTU_SUPPORT=1'],
            ['WEB_INTERFACE_ENABLED=0','WEB_LIVE_SUPPORT=1'],
            ['WEB_INTERFACE_ENABLED=0','WEB_PLAIN_HTML_SUPPORT=1'],
            ['WEB_INTERFACE_ENABLED=0','COMMAND_API_SUPPORT=0'],
            ['HUD_SUPPORT=0','HUD_UART_SUPPORT=1'],
            ['HUD_SUPPORT=0','HUD_UDP_SUPPORT=1'],
            ['OTA_SUPPORT=0','ARDUINO_OTA_SUPPORT=1']]:
            self.compile({},definitions,success=False)
        self.compile(dict(GRAPHICAL_DISPLAY=0),['BOARD_PROFILE=10','DISPLAY_KIND=5','DISPLAY_SUPPORT=0'])
        self.compile(dict(CAMERA_ENABLED=0),['BOARD_PROFILE=7','WEB_INTERFACE_ENABLED=0'])

    def test_disabled_display_omits_dependencies(self):
        self.edit(DISPLAY_KIND='DISPLAY_ILI9341',DISPLAY_SUPPORT='0')
        result=subprocess.run([sys.executable,str(self.root/'tools/build_platformio.py'),'--dry-run'],text=True,capture_output=True)
        self.assertEqual(result.returncode,0,result.stderr)
        config=configparser.ConfigParser(interpolation=None);config.read(self.root/'build/platformio-config/esp32s3_n16r8.ini')
        self.assertEqual(config.get('env:selected','lib_deps').strip(),'')
        for board,display in [(1,1),(5,3),(8,4),(10,5)]:
            result=subprocess.run(['g++','-std=c++17','-fsyntax-only','-I',str(self.source),
                                   f'-DBOARD_PROFILE={board}',f'-DDISPLAY_KIND={display}',
                                   '-DDISPLAY_SUPPORT=0',str(self.source/'display_spi.cpp')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)

    def test_service_capabilities(self):
        self.compile(dict(WEB_INTERFACE_ENABLED=1, BLUETOOTH_SUPPORT=1))
        self.compile(dict(WEB_INTERFACE_ENABLED=0, BLUETOOTH_SUPPORT=0),
                     ['WEB_INTERFACE_ENABLED=0', 'BLUETOOTH_SUPPORT=0'])
        self.compile({}, ['BLUETOOTH_SUPPORT=0', 'BLUETOOTH_ENABLED=1'], success=False)
        self.compile({}, ['WEB_INTERFACE_ENABLED=2'], success=False)
        config = configparser.ConfigParser(interpolation=None)
        config.read(ROOT / 'platformio.ini')
        flags = config['env:esp32_4mb_noweb']['build_flags']
        self.assertIn('-D WEB_INTERFACE_ENABLED=0', flags)
        self.assertIn('-D BLUETOOTH_SUPPORT=0', flags)
        self.assertIn('partitions_s2.csv', config['env:esp32_4mb_noweb']['board_build.partitions'])

    def test_simulation_releases_pins_on_every_board(self):
        source = '''#include "board.hpp"
#include <cassert>
using namespace forkrot;
int main() {
  for(unsigned drive=0;drive<=DRIVE_DIR_PWM;++drive) {
    HardwareConfig config;config.drive=drive;config.simulation=true;
    for(const auto &d:pinDefinitions) config.pins.*d.member=1;
    const auto original=config;
    auto physical=config;physical.simulation=false;
    physical.releaseSimulationPins();assert(physical.pins==original.pins);
    config.releaseSimulationPins();
    for(const auto &d:pinDefinitions) {
      const bool motor=d.group==PinGroup::Drive || d.group==PinGroup::SensorAz || d.group==PinGroup::SensorEl;
      assert(config.pins.*d.member==(motor?-1:original.pins.*d.member));
      if(motor)assert(!board::pinActive(config,d));
    }
    const auto released=config;config.releaseSimulationPins();assert(config==released);
  }
  HardwareConfig hud;hud.simulation=false;hud.bluetooth=false;
  for(const auto &d:pinDefinitions)
    if(d.group==PinGroup::Drive || d.group==PinGroup::SensorAz || d.group==PinGroup::SensorEl) {
      hud.pins.*d.member=-1;assert(!board::pinActive(hud,d,true));
    }
  assert(!board::validateHardware(hud,false,true));
}'''
        for board in range(1, 11):
            with self.subTest(board=board):
                binary = self.root / ('simulation-pins-' + str(board))
                flags = [f'-DBOARD_PROFILE={board}', '-DBLUETOOTH_ENABLED=0',
                         '-DUSER_BUTTON_GPIO=-1', '-DDRIVE_SIMULATION=1',
                         f'-DDISPLAY_KIND={ {5:3, 8:4, 10:5}.get(board,0)}']
                result = subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++17',
                                         '-I', str(self.source), *flags, '-x', 'c++', '-',
                                         '-o', str(binary)], input=source, text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                subprocess.run([str(binary)], check=True)

    def test_defaults(self):
        self.compile(dict(BOARD_PROFILE=0, SELECTED_BOARD=1, DISPLAY_KIND=0, TFT_DISPLAY_ENABLED=0,
                          BLUETOOTH_ENABLED=0, BLUETOOTH_BACKEND=0, BLUETOOTH_MODE=3,
                          DRIVE_SIMULATION=0, DRIVE_INTERFACE=0, CROWPANEL_USB_TRACE=0,
                          ESP32_FORCE_AP_STA=0, **{'forkrot::board::WIFI_AP_ALWAYS_ON': 0},
                          BTS7960_PWM_INVERTED=0, USER_BUTTON_GPIO=0, USER_BUTTON_ACTIVE_LOW=1,
                          USER_BUTTON_PULLUP=1, AZ_STOP_TOLERANCE_DEG='5.0f', EL_STOP_TOLERANCE_DEG='5.0f'))

    def test_round_c3_profile(self):
        self.compile(dict(SELECTED_BOARD=8, FORK_C3=1, FORK_S3=0, FORK_CLASSIC_ESP32=0,
                          USER_BUTTON_GPIO=9, DRIVE_SIMULATION=0, TFT_DISPLAY_ENABLED=0),
                     ['BOARD_PROFILE=BOARD_ESP32_2424S012C','CONFIG_IDF_TARGET_ESP32C3=1','DISPLAY_KIND=4'])
        result=subprocess.run([sys.executable,str(self.root/'tools/build_platformio.py'),
                               '--env','esp32_2424s012c','--dry-run'],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        config=configparser.ConfigParser(interpolation=None)
        config.read(self.root/'build/platformio-config/esp32_2424s012c.ini')
        selected=config['env:selected']
        self.assertIn('-DBOARD_PROFILE=BOARD_ESP32_2424S012C',selected['build_flags'])
        self.assertIn('-DDISPLAY_KIND=4',selected['build_flags'])
        self.assertIn('Adafruit GC9A01A @ 1.1.0',selected['lib_deps'])
        self.assertIn('lvgl/lvgl @ 9.6.0',selected['lib_deps'])
        self.assertIn('-DLV_CONF_INCLUDE_SIMPLE',selected['build_flags'])
        self.assertEqual(selected['lib_ldf_mode'], 'chain')
        result=subprocess.run([sys.executable,str(self.root/'tools/prepare_sketch.py'),
                               '--target','esp32-2424s012c'],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        sketch=self.root/'build/arduino-esp32-2424s012c/G5500_Fork'
        self.assertEqual((sketch/'partitions.csv').read_bytes(),(self.source/'partitions_esp32.csv').read_bytes())

    def test_st7796_profile(self):
        self.compile(dict(SELECTED_BOARD=10, FORK_CLASSIC_ESP32=1, FORK_S3=0, FORK_C3=0,
                          GRAPHICAL_DISPLAY=1, PANEL_COMPACT_FONTS=1, TFT_DISPLAY_ENABLED=0),
                     ['BOARD_PROFILE=BOARD_ESP32_3248S035C','DISPLAY_KIND=DISPLAY_ST7796',
                      'CONFIG_IDF_TARGET_ESP32=1'])
        self.compile({}, ['BOARD_PROFILE=10','DISPLAY_KIND=1'], success=False)
        self.compile({}, ['BOARD_PROFILE=4','DISPLAY_KIND=5'], success=False)
        self.compile({}, ['BOARD_PROFILE=10','DISPLAY_KIND=5','CONFIG_IDF_TARGET_ESP32C3=1'], success=False)
        result=subprocess.run([sys.executable,str(self.root/'tools/build_platformio.py'),
                               '--env','esp32_3248s035c','--display','st7796','--dry-run'],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        config=configparser.ConfigParser(interpolation=None)
        config.read(self.root/'build/platformio-config/esp32_3248s035c.ini')
        selected=config['env:selected']
        self.assertIn('-DDISPLAY_KIND=5',selected['build_flags'])
        self.assertIn('-flto',selected['build_flags'])
        self.assertIn('lvgl',selected['lib_deps'])
        self.assertNotIn('Adafruit',selected['lib_deps'])
        result=subprocess.run([sys.executable,str(self.root/'tools/prepare_sketch.py'),
                               '--target','esp32-3248s035c'],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        sketch=self.root/'build/arduino-esp32-3248s035c/G5500_Fork'
        self.assertEqual((sketch/'partitions.csv').read_bytes(),(self.source/'partitions_esp32.csv').read_bytes())
        self.assertTrue((sketch/'display_st7796.cpp').is_file())

    def test_headless_c3_profile(self):
        expected=dict(SELECTED_BOARD=9,FORK_C3=1,FORK_S3=0,FORK_CLASSIC_ESP32=0,
                      DISPLAY_KIND=0,USER_BUTTON_GPIO=9,DRIVE_SIMULATION=0,
                      **{'forkrot::board::PIN_LEFT':-1,'forkrot::board::PIN_AZ_ADC':-1})
        self.compile(expected,['CONFIG_IDF_TARGET_ESP32C3=1'])
        self.compile(expected,['BOARD_PROFILE=9'])
        result=subprocess.run([sys.executable,str(self.root/'tools/build_platformio.py'),
                               '--env','esp32c3_4mb','--dry-run'],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        config=configparser.ConfigParser(interpolation=None)
        config.read(self.root/'build/platformio-config/esp32c3_4mb.ini')
        selected=config['env:selected']
        self.assertIn('-DBOARD_PROFILE=BOARD_ESP32_C3',selected['build_flags'])
        self.assertIn('-DDISPLAY_KIND=0',selected['build_flags'])
        self.assertEqual(selected['lib_deps'].strip(),'')
        result=subprocess.run([sys.executable,str(self.root/'tools/prepare_sketch.py'),
                               '--target','esp32-c3'],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        sketch=self.root/'build/arduino-esp32-c3/G5500_Fork'
        self.assertEqual((sketch/'partitions.csv').read_bytes(),(self.source/'partitions_esp32.csv').read_bytes())
        self.assertTrue((sketch/'board_esp32c3.hpp').is_file())

    def test_crowpanel_usb_trace_profile(self):
        flags = ['BOARD_PROFILE=5', 'DISPLAY_KIND=3', 'USER_BUTTON_GPIO=-1',
                 'BLUETOOTH_ENABLED=0', 'CROWPANEL_USB_TRACE=1']
        self.compile(dict(CROWPANEL_USB_TRACE=1), flags)
        self.compile({}, flags + ['CROWPANEL_TOUCH_TRACE=1'], success=False)
        self.compile({}, ['CROWPANEL_USB_TRACE=1'], success=False)
        self.compile({}, ['CROWPANEL_USB_TRACE=2'], success=False)
        config = configparser.ConfigParser(interpolation=None)
        config.read(ROOT / 'platformio.ini')
        self.assertEqual(config['env:crowpanel_21_trace']['extends'], 'env:crowpanel_21')
        self.assertIn('-D CROWPANEL_USB_TRACE=1', config['env:crowpanel_21_trace']['build_flags'])

    def test_edited_settings_and_override_priority(self):
        edited = dict(BOARD_PROFILE=3, DISPLAY_KIND=2, BLUETOOTH_ENABLED=0, DRIVE_INTERFACE=2,
                      BTS7960_PWM_INVERTED=1, USER_BUTTON_GPIO=38, USER_BUTTON_ACTIVE_LOW=0,
                      USER_BUTTON_PULLUP=0, AZ_STOP_TOLERANCE_DEG='7.0f', EL_STOP_TOLERANCE_DEG='4.0f')
        self.edit(**edited)
        self.compile(dict(edited, SELECTED_BOARD=3, BLUETOOTH_BACKEND=0, TFT_DISPLAY_ENABLED=0))
        overridden = dict(BOARD_PROFILE=1, DISPLAY_KIND=0, BLUETOOTH_ENABLED=1, DRIVE_INTERFACE=0,
                          BTS7960_PWM_INVERTED=0, USER_BUTTON_GPIO=0, USER_BUTTON_ACTIVE_LOW=1,
                          USER_BUTTON_PULLUP=1, AZ_STOP_TOLERANCE_DEG='5.0f', EL_STOP_TOLERANCE_DEG='5.0f')
        self.compile(dict(overridden, SELECTED_BOARD=1, BLUETOOTH_BACKEND=1, TFT_DISPLAY_ENABLED=0),
                     [f'{name}={value}' for name, value in overridden.items()])

    def test_legacy_display_override_and_conflict(self):
        self.edit(DISPLAY_KIND='DISPLAY_LCD1602')
        for kind in (0, 1):
            self.compile(dict(DISPLAY_KIND=kind, TFT_DISPLAY_ENABLED=kind), [f'TFT_DISPLAY_ENABLED={kind}'])
        self.compile({}, ['DISPLAY_KIND=DISPLAY_LCD1602', 'TFT_DISPLAY_ENABLED=1'], success=False)

    def test_auto_boards(self):
        for flag, board, backend in [('ARDUINO_LOLIN_S2_MINI=1', 3, 1),
                                     ('CONFIG_IDF_TARGET_ESP32S2=1', 2, 1),
                                     ('CONFIG_IDF_TARGET_ESP32S3=1', 1, 1),
                                     ('CONFIG_IDF_TARGET_ESP32=1', 4, 2)]:
            with self.subTest(flag=flag):
                self.compile(dict(SELECTED_BOARD=board, BLUETOOTH_BACKEND=backend, DRIVE_SIMULATION=0,
                                  **{'forkrot::board::WIFI_AP_ALWAYS_ON': 0}), [flag, 'BLUETOOTH_ENABLED=1'])

    def test_wt32_ethernet_profile_and_arduino_copy(self):
        self.compile(dict(SELECTED_BOARD=6, FORK_CLASSIC_ESP32=1, USER_BUTTON_GPIO=-1,
                          BLUETOOTH_BACKEND=2, **{'forkrot::board::PIN_AZ_ADC':36}),
                     ['ARDUINO_WT32_ETH01=1','CONFIG_IDF_TARGET_ESP32=1','BLUETOOTH_ENABLED=1'])
        for name in ('wt32_eth01','wt32_eth01_noweb'):
            result=subprocess.run([sys.executable,str(self.root/'tools/build_platformio.py'),
                                   '--env',name,'--dry-run'],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            config=configparser.ConfigParser(interpolation=None)
            config.read(self.root/f'build/platformio-config/{name}.ini')
            self.assertIn('-DBOARD_PROFILE=BOARD_WT32_ETH01',config['env:selected']['build_flags'])
            self.assertEqual(config['env:selected']['lib_deps'],'')
            if name.endswith('_noweb'):
                self.assertIn('-DWEB_INTERFACE_ENABLED=0',config['env:selected']['build_flags'])
        result=subprocess.run([sys.executable,str(self.root/'tools/prepare_sketch.py'),
                               '--target','wt32-eth01'],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        sketch=self.root/'build/arduino-wt32-eth01/G5500_Fork'
        self.assertEqual((sketch/'partitions.csv').read_bytes(),(self.source/'partitions_esp32.csv').read_bytes())
        self.assertEqual((sketch/'board_wt32_eth01.hpp').read_bytes(),(self.source/'board_wt32_eth01.hpp').read_bytes())

    def test_waveshare_camera_and_motor_profile(self):
        self.compile(dict(SELECTED_BOARD=7, FORK_S3=1, CAMERA_ENABLED=1, DRIVE_SIMULATION=0,
                          **{'forkrot::board::PIN_AZ_ADC':4,'forkrot::board::PIN_EL_ADC':5,
                             'forkrot::board::PIN_UART_RX':-1,'forkrot::board::PIN_UART_TX':-1}),
                     ['BOARD_PROFILE=7','CONFIG_IDF_TARGET_ESP32S3=1','DRIVE_INTERFACE=2'])
        self.compile(dict(CAMERA_ENABLED=0), ['BOARD_PROFILE=7','CAMERA_ENABLED=0'])
        for flags in (['BOARD_PROFILE=1','CAMERA_ENABLED=1'],
                      ['BOARD_PROFILE=7','WEB_INTERFACE_ENABLED=0','CAMERA_ENABLED=1'],
                      ['BOARD_PROFILE=7','DISPLAY_KIND=1'],
                      ['BOARD_PROFILE=7','USER_BUTTON_GPIO=8']):
            self.compile({},flags,success=False)
        for camera in (0,1):
            for drive in (0,1,2,3):
                self.compile({},['BOARD_PROFILE=7',f'CAMERA_ENABLED={camera}',f'DRIVE_INTERFACE={drive}'])
        result=subprocess.run([sys.executable,str(self.root/'tools/build_platformio.py'),
                               '--env','waveshare_esp32s3_eth','--dry-run'],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        config=configparser.ConfigParser(interpolation=None)
        config.read(self.root/'build/platformio-config/waveshare_esp32s3_eth.ini')
        self.assertIn('-DBOARD_PROFILE=BOARD_WAVESHARE_S3_ETH',config['env:selected']['build_flags'])
        self.assertIn('-DDRIVE_INTERFACE=DRIVE_BTS7960',config['env:selected']['build_flags'])
        result=subprocess.run([sys.executable,str(self.root/'tools/prepare_sketch.py'),
                               '--target','waveshare-s3-eth'],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        sketch=self.root/'build/arduino-waveshare-s3-eth/G5500_Fork'
        self.assertEqual((sketch/'partitions.csv').read_bytes(),(self.source/'partitions_s3.csv').read_bytes())

    def test_simulation_and_external_bluetooth(self):
        self.edit(BOARD_PROFILE=4, BLUETOOTH_ENABLED=1, BLUETOOTH_MODE='BT_HC05', DRIVE_SIMULATION=1)
        self.compile(dict(BLUETOOTH_BACKEND=1, DRIVE_SIMULATION=1))
        self.compile(dict(BLUETOOTH_BACKEND=2, DRIVE_SIMULATION=0),
                     ['BLUETOOTH_MODE=BT_ESP32_SPP', 'DRIVE_SIMULATION=0'])
        self.compile(dict(BLUETOOTH_BACKEND=0), ['BLUETOOTH_ENABLED=0'])
        # Old command-line flag remains usable, with the new flag taking precedence.
        self.compile(dict(DRIVE_SIMULATION=0), ['CROWPANEL_SIMULATION=0'])
        self.compile(dict(DRIVE_SIMULATION=1), ['CROWPANEL_SIMULATION=0', 'DRIVE_SIMULATION=1'])
        config = self.selected_config('--bluetooth', '1', '--bluetooth-mode', 'hc05', '--simulation', '1')
        self.assertIn('-DBLUETOOTH_MODE=BT_HC05', config['build_flags'])
        self.assertIn('-DDRIVE_SIMULATION=1', config['build_flags'])

    def test_wifi_ap_policy_by_board_and_override(self):
        for board in (1, 2, 3, 4):
            for force in (0, 1):
                with self.subTest(board=board, force=force):
                    self.compile({'forkrot::board::WIFI_AP_ALWAYS_ON': force},
                                 [f'BOARD_PROFILE={board}', f'ESP32_FORCE_AP_STA={force}'])
        self.edit(BOARD_PROFILE=4, ESP32_FORCE_AP_STA=1)
        self.compile({'forkrot::board::WIFI_AP_ALWAYS_ON': 1})
        self.compile({'forkrot::board::WIFI_AP_ALWAYS_ON': 0}, ['ESP32_FORCE_AP_STA=0'])
        self.compile({'forkrot::board::WIFI_AP_ALWAYS_ON': 0},
                     ['ESP32_FORCE_AP_STA=0', 'BLUETOOTH_ENABLED=0'])
        for invalid in (-1, 2):
            self.compile({}, [f'ESP32_FORCE_AP_STA={invalid}'], success=False)

    def selected_config(self, *args):
        result = subprocess.run([sys.executable, str(self.root / 'tools/build_platformio.py'),
                                 '--env', 'esp32_4mb', '--dry-run', *args], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        config = configparser.ConfigParser(interpolation=None)
        config.read(self.root / 'build/platformio-config/esp32_4mb.ini')
        return config['env:selected']

    def test_motion_rate_build_option(self):
        self.assertNotIn('MOTION_CONTROL_PERIOD_MS', self.selected_config()['build_flags'])
        for hz, period in ((50,20),(100,10),(200,5)):
            config = self.selected_config('--motion-hz', str(hz))
            self.assertIn(f'-DMOTION_CONTROL_PERIOD_MS={period}', config['build_flags'])
        for period in (5,10,20,0,1,7,25):
            result = subprocess.run(['g++','-std=c++17','-fsyntax-only','-I',str(self.source),
                                     f'-DMOTION_CONTROL_PERIOD_MS={period}','-x','c++','-'],
                                    input='#include "motion_timing.hpp"\n', text=True, capture_output=True)
            self.assertEqual(result.returncode == 0, period in (5,10,20), result.stderr)

    def test_dependencies_follow_formatted_settings_and_flags(self):
        self.edit(DISPLAY_KIND='DISPLAY_LCD1602', BLUETOOTH_ENABLED=0)
        config = self.selected_config()
        self.assertIn('-DDISPLAY_KIND=2', config['build_flags'])
        self.assertIn('LiquidCrystal', config['lib_deps'])
        self.assertNotIn('BluetoothSerial', config['lib_deps'])
        self.assertNotIn('ILI9341', config['lib_deps'])
        config = self.selected_config('--display', 'tft', '--bluetooth', '1')
        self.assertIn('-DDISPLAY_KIND=1', config['build_flags'])
        self.assertIn('-DWEB_PLAIN_HTML_SUPPORT=0', config['build_flags'])
        self.assertIn('-DBLUETOOTH_ENABLED=1', config['build_flags'])
        self.assertNotIn('BluetoothSerial', config['lib_deps'])
        self.assertIn('-DBLUETOOTH_ENABLED=1', config['build_flags'])
        self.assertIn('ILI9341', config['lib_deps'])
        self.assertNotIn('LiquidCrystal', config['lib_deps'])

    def test_arduino_copy_contains_config_headers(self):
        result = subprocess.run([sys.executable, str(self.root / 'tools/prepare_sketch.py'),
                                 '--target', 's2-mini'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        sketch = self.root / 'build/arduino-s2-mini/G5500_Fork'
        for name in ['build_config.hpp', 'build_config_options.hpp', 'build_config_checks.hpp', 'runtime_defaults.hpp', 'hardware_config.hpp']:
            self.assertEqual((sketch / name).read_bytes(), (self.source / name).read_bytes())
        self.assertEqual((sketch / 'partitions.csv').read_bytes(), (self.source / 'partitions_esp32.csv').read_bytes())
        config = configparser.ConfigParser(interpolation=None)
        config.read(self.root / 'platformio.ini')
        mini = config['env:esp32s2_mini']
        self.assertEqual(int(mini['board_upload.maximum_size']), 3 << 20)
        self.assertEqual(mini['board_build.partitions'], 'G5500_Fork/partitions_esp32.csv')
        for variant in ('_tft', '_lcd', '_headless'):
            self.assertEqual(config['env:esp32s2_mini' + variant]['extends'], 'env:esp32s2_mini')
        # The former S2 wiring keeps its independent two-slot layout.
        self.assertEqual(config['env:esp32s2_4mb']['board_build.partitions'], 'G5500_Fork/partitions_s2.csv')
        for path in (self.source / 'src').rglob('*'):
            if path.is_file():
                self.assertEqual((sketch / path.relative_to(self.source)).read_bytes(), path.read_bytes())

    def test_crowpanel_profile_dependencies_and_arduino_copy(self):
        self.compile({'SELECTED_BOARD': 5, 'TFT_DISPLAY_ENABLED': 0, 'DRIVE_SIMULATION': 1,
                      'forkrot::board::PIN_AZ_ADC': -1, 'forkrot::board::PIN_LEFT': -1},
                     ['BOARD_PROFILE=5', 'DISPLAY_KIND=3', 'USER_BUTTON_GPIO=-1', 'BLUETOOTH_ENABLED=0'])
        result = subprocess.run([sys.executable, str(self.root / 'tools/build_platformio.py'),
                                 '--env', 'crowpanel_21', '--dry-run'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        config = configparser.ConfigParser(interpolation=None)
        config.read(self.root / 'build/platformio-config/crowpanel_21.ini')
        selected = config['env:selected']
        self.assertIn('lvgl/lvgl @ 9.6.0', selected['lib_deps'])
        self.assertIn('-DDISPLAY_KIND=3', selected['build_flags'])
        self.assertIn('-DUSER_BUTTON_GPIO=-1', selected['build_flags'])
        self.assertNotIn('ILI9341', selected['lib_deps'])
        result = subprocess.run([sys.executable, str(self.root / 'tools/prepare_sketch.py'),
                                 '--target', 'crowpanel'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        sketch = self.root / 'build/arduino-crowpanel/G5500_Fork'
        for name in ('lv_conf.h', 'crowpanel_lcd_init.hpp', 'crowpanel_font_36.cpp', 'crowpanel_font_112.cpp', 'crowpanel_rgb_driver.c', 'crowpanel_rgb_driver.h'):
            self.assertEqual((sketch / name).read_bytes(), (self.source / name).read_bytes())
        self.assertEqual((sketch / 'partitions.csv').read_bytes(), (self.source / 'partitions_s3.csv').read_bytes())

    def test_crowpanel_local_az_profile(self):
        flags = ['BOARD_PROFILE=5', 'DISPLAY_KIND=3', 'CROWPANEL_LOCAL_DRIVE=1',
                 'USER_BUTTON_GPIO=-1', 'BLUETOOTH_ENABLED=0', 'DRIVE_SIMULATION=0']
        for drive in (0, 2):
            self.compile({'forkrot::board::PIN_AZ_ADC': 6,
                          'forkrot::board::PIN_BACKLIGHT': -1,
                          'forkrot::board::PIN_SPEED': 0,
                          'forkrot::board::PIN_LEFT': 44,
                          'forkrot::board::PIN_RIGHT': 43,
                          'forkrot::board::PIN_UART_RX': -1,
                          'forkrot::board::PIN_UART_TX': -1}, flags + [f'DRIVE_INTERFACE={drive}'])
        result = subprocess.run([sys.executable, str(self.root / 'tools/build_platformio.py'),
                                 '--env', 'crowpanel_21_az', '--drive', 'bts7960', '--dry-run'],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        config = configparser.ConfigParser(interpolation=None)
        config.read(self.root / 'build/platformio-config/crowpanel_21_az.ini')
        self.assertIn('-DCROWPANEL_LOCAL_DRIVE=1', config['env:selected']['build_flags'])
        self.assertIn('-DDRIVE_INTERFACE=2', config['env:selected']['build_flags'])
        self.assertIn('lvgl', config['env:selected']['lib_deps'])

    def test_legacy_s2_preserves_services_or_enables_mqtt_compact(self):
        for profile,mqtt,compact in [('esp32s2_4mb',False,False),('esp32s2_4mb_headless',False,False),
                                     ('esp32s2_4mb_ota_compact',True,True)]:
            result=subprocess.run([sys.executable,str(self.root/'tools/build_platformio.py'),
                                   '--env',profile,'--dry-run'],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            config=configparser.ConfigParser(interpolation=None)
            config.read(self.root/f'build/platformio-config/{profile}.ini')
            flags=config['env:selected']['build_flags']
            self.assertEqual('-DMQTT_SUPPORT=0' not in flags,mqtt)
            self.assertEqual('-DWEB_LIVE_SUPPORT=0' in flags,compact)
            self.assertEqual('-DARDUINO_OTA_SUPPORT=0' in flags,compact)

    def test_esp32_ota_partitions_and_arduino_copy(self):
        for name, flash, count in [('partitions_s2.csv', 4 << 20, 2),
                                   ('partitions_esp32_8mb.csv', 8 << 20, 2),
                                   ('partitions_esp32.csv', 4 << 20, 1)]:
            previous = 0
            slots = []
            for line in (self.source / name).read_text().splitlines():
                if not line.strip() or line.startswith('#'):
                    continue
                fields = [s.strip() for s in line.split(',')]
                offset, size = int(fields[3], 0), int(fields[4], 0)
                self.assertGreaterEqual(offset, previous)
                self.assertLessEqual(offset + size, flash)
                previous = offset + size
                if fields[1] == 'app':
                    self.assertEqual(offset % 65536, 0)
                    if count == 1:
                        self.assertEqual(fields[2], 'factory')
                        self.assertEqual(offset, 0x10000)
                    slots.append(size)
                if fields[2] == 'nvs':
                    self.assertEqual((offset, size), (0x9000, 0x5000))
            self.assertEqual(len(slots), count)
            if count == 2:
                self.assertEqual(slots[0], slots[1])
            else:
                self.assertEqual(slots[0], 3 << 20)
        result = subprocess.run([sys.executable, str(self.root / 'tools/prepare_sketch.py'),
                                 '--target', 'esp32-8mb'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        sketch = self.root / 'build/arduino-esp32-8mb/G5500_Fork'
        self.assertEqual((sketch / 'partitions.csv').read_bytes(), (self.source / 'partitions_esp32_8mb.csv').read_bytes())


if __name__ == '__main__':
    unittest.main()
