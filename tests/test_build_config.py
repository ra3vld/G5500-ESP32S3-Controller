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
        (self.root / 'tools').mkdir()
        for name in ['build_platformio.py', 'prepare_sketch.py', 'generate_web_assets.py']:
            shutil.copy2(ROOT / 'tools' / name, self.root / 'tools' / name)
        shutil.copy2(ROOT / 'platformio.ini', self.root / 'platformio.ini')

    def edit(self, **settings):
        path = self.source / 'build_config.hpp'
        text = path.read_text()
        for name, value in settings.items():
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

    def test_defaults(self):
        self.compile(dict(BOARD_PROFILE=0, SELECTED_BOARD=1, DISPLAY_KIND=0, TFT_DISPLAY_ENABLED=0,
                          BLUETOOTH_ENABLED=1, BLUETOOTH_BACKEND=1, DRIVE_INTERFACE=0,
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
                self.compile(dict(SELECTED_BOARD=board, BLUETOOTH_BACKEND=backend,
                                  **{'forkrot::board::WIFI_AP_ALWAYS_ON': int(board != 4)}), [flag])

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
        for name in ['build_config.hpp', 'build_config_options.hpp', 'build_config_checks.hpp']:
            self.assertEqual((sketch / name).read_bytes(), (self.source / name).read_bytes())
        self.assertEqual((sketch / 'partitions.csv').read_bytes(), (self.source / 'partitions_s2.csv').read_bytes())

    def test_crowpanel_profile_dependencies_and_arduino_copy(self):
        self.compile({'SELECTED_BOARD': 5, 'TFT_DISPLAY_ENABLED': 0,
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


if __name__ == '__main__':
    unittest.main()
