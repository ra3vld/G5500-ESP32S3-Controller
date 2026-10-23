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
        for name in ['build_platformio.py', 'prepare_sketch.py', 'generate_web_assets.py']:
            shutil.copy2(ROOT / 'tools' / name, self.root / 'tools' / name)
        shutil.copy2(ROOT / 'platformio.ini', self.root / 'platformio.ini')

    def edit(self, **settings):
        for name, value in settings.items():
            path = self.source / ('build_config.hpp' if name in {'BOARD_PROFILE', 'DISPLAY_KIND'} else 'runtime_defaults.hpp')
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

    def test_defaults(self):
        self.compile(dict(BOARD_PROFILE=0, SELECTED_BOARD=1, DISPLAY_KIND=0, TFT_DISPLAY_ENABLED=0,
                          BLUETOOTH_ENABLED=0, BLUETOOTH_BACKEND=0, BLUETOOTH_MODE=3,
                          DRIVE_SIMULATION=0, DRIVE_INTERFACE=0,
                          ESP32_FORCE_AP_STA=0, **{'forkrot::board::WIFI_AP_ALWAYS_ON': 1},
                          BTS7960_PWM_INVERTED=0, USER_BUTTON_GPIO=0, USER_BUTTON_ACTIVE_LOW=1,
                          USER_BUTTON_PULLUP=1, AZ_STOP_TOLERANCE_DEG='5.0f', EL_STOP_TOLERANCE_DEG='5.0f'))

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
                                  **{'forkrot::board::WIFI_AP_ALWAYS_ON': int(board != 4)}), [flag, 'BLUETOOTH_ENABLED=1'])

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
                      ['BOARD_PROFILE=7','WEB_INTERFACE_ENABLED=0'],
                      ['BOARD_PROFILE=7','DISPLAY_KIND=1'],
                      ['BOARD_PROFILE=7','USER_BUTTON_GPIO=8']):
            self.compile({},flags,success=False)
        for camera in (0,1):
            for drive in (0,1,2):
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
                    self.compile({'forkrot::board::WIFI_AP_ALWAYS_ON': int(board != 4 or force)},
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
        self.assertEqual((sketch / 'partitions.csv').read_bytes(), (self.source / 'partitions_s2.csv').read_bytes())
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

    def test_esp32_ota_partitions_and_arduino_copy(self):
        for name, flash in [('partitions_s2.csv', 4 << 20), ('partitions_esp32_8mb.csv', 8 << 20)]:
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
                    slots.append(size)
            self.assertEqual(len(slots), 2)
            self.assertEqual(slots[0], slots[1])
        result = subprocess.run([sys.executable, str(self.root / 'tools/prepare_sketch.py'),
                                 '--target', 'esp32-8mb'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        sketch = self.root / 'build/arduino-esp32-8mb/G5500_Fork'
        self.assertEqual((sketch / 'partitions.csv').read_bytes(), (self.source / 'partitions_esp32_8mb.csv').read_bytes())


if __name__ == '__main__':
    unittest.main()
