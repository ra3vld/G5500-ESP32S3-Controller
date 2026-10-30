from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from build_capabilities import partition_capabilities, write_capabilities


class BuildCapabilitiesTests(unittest.TestCase):
    def test_repository_partitions(self):
        for name, flash, ota in [('esp32', '4MB', 0), ('esp32_8mb', '8MB', 1),
                                 ('s2', '4MB', 1), ('s3', '16MB', 1)]:
            values = partition_capabilities(ROOT / f'G5500_Fork/partitions_{name}.csv', flash)
            self.assertEqual(values['G5500_HAS_OTA_PARTITIONS'], ota)
            self.assertEqual(values['G5500_BUILD_FLASH_BYTES'], int(flash[:-2]) << 20)

    def test_metadata_and_distinct_ota_slots_required(self):
        with tempfile.TemporaryDirectory() as directory:
            table = Path(directory) / 'partitions.csv'
            rows = ['meta,0x01,0x0,0xe000,8K\n', 'a,0,0x10,0x10000,1M\n', 'b,app,ota_1,,1M\n']
            for text, expected in [(''.join(rows), 1), (''.join(rows[1:]), 0),
                                   (''.join(rows[:2]), 0), (rows[0]+rows[1]*2, 0),
                                   (''.join(rows).replace('8K', '4K'), 0)]:
                table.write_text(text)
                self.assertEqual(partition_capabilities(table, '4194304')['G5500_HAS_OTA_PARTITIONS'], expected)

    def test_effective_firmware_defaults_and_overrides(self):
        # Let the C++ preprocessor evaluate actual profile/default expressions.
        for board, flash, slots, ota, plain in [(4, 4, 0, 0, 0), (9, 4, 0, 0, 0),
                                               (4, 8, 1, 1, 1), (1, 16, 1, 1, 1)]:
            with self.subTest(board=board, flash=flash):
                assertions = '#include "build_config.hpp"\n' + f'static_assert(bool(ARDUINO_OTA_SUPPORT)=={ota});\nstatic_assert(bool(WEB_PLAIN_HTML_SUPPORT)=={plain});\n'
                cmd = ['g++', '-std=c++17', '-fsyntax-only', '-x', 'c++', '-I', str(ROOT/'G5500_Fork'),
                       f'-DBOARD_PROFILE={board}', f'-DG5500_BUILD_FLASH_BYTES={flash << 20}',
                       f'-DG5500_HAS_OTA_PARTITIONS={slots}', '-']
                subprocess.run(cmd, input=assertions, text=True, check=True, capture_output=True)
                forced = '#include "build_config.hpp"\nstatic_assert(WEB_PLAIN_HTML_SUPPORT==1);\nstatic_assert(ARDUINO_OTA_SUPPORT==0);\n'
                subprocess.run(cmd[:-1]+['-DWEB_PLAIN_HTML_SUPPORT=1', '-DARDUINO_OTA_SUPPORT=0', '-'],
                               input=forced, text=True, check=True, capture_output=True)

    def test_platformio_uses_selected_partition_and_flash(self):
        class Environment:
            partition = 'G5500_Fork/partitions_esp32_8mb.csv'
            flash = '8MB'
            defines = []
            def subst(self, key):
                return str(ROOT if key == '$PROJECT_DIR' else ROOT/'G5500_Fork')
            def BoardConfig(self):
                return {'upload.flash_size': '4MB', 'build.partitions': 'unused.csv'}
            def GetProjectOption(self, key, default):
                return {'board_build.partitions': self.partition,
                        'board_upload.flash_size': self.flash, 'build_flags': self.defines}.get(key, default)
            def Append(self, **values):
                self.values = values
            def ParseFlagsExtended(self, flags):
                return {'CPPDEFINES': self.defines}
        env = Environment()
        with patch('subprocess.run'):
            runpy.run_path(str(ROOT/'tools/platformio_version.py'), init_globals={'env': env, 'Import': lambda _: None})
        self.assertEqual(dict(env.values['CPPDEFINES']), {'G5500_BUILD_FLASH_BYTES': 8 << 20, 'G5500_HAS_OTA_PARTITIONS': 1})
        env.partition = 'G5500_Fork/partitions_esp32.csv'
        env.flash = '4MB'
        with patch('subprocess.run'):
            runpy.run_path(str(ROOT/'tools/platformio_version.py'), init_globals={'env': env, 'Import': lambda _: None})
        self.assertEqual(dict(env.values['CPPDEFINES'])['ARDUINO_OTA_SUPPORT'], 0)
        env.defines = [('ARDUINO_OTA_SUPPORT', 1)]
        with patch('subprocess.run'):
            runpy.run_path(str(ROOT/'tools/platformio_version.py'), init_globals={'env': env, 'Import': lambda _: None})
        self.assertNotIn('ARDUINO_OTA_SUPPORT', dict(env.values['CPPDEFINES']))

    def test_reprepared_sketch_removes_retired_cat_assets(self):
        with tempfile.TemporaryDirectory() as directory:
            sketch = Path(directory)/'G5500_Fork'
            sketch.mkdir()
            retired = ('cat_image.h', 'tft_image.hpp', 'tft_image_rows.hpp', 'web_hud.cpp')
            for name in retired:
                (sketch/name).write_text('retired asset')
            subprocess.run([sys.executable, str(ROOT/'tools/prepare_sketch.py'), '--target', 'esp32-c3',
                            '--output', str(sketch)], check=True, capture_output=True)
            self.assertTrue(all(not (sketch/name).exists() for name in retired))
            header = (sketch/'build_capabilities.hpp').read_text()
            self.assertIn('#define G5500_HAS_OTA_PARTITIONS 0', header)
            self.assertIn('#define G5500_BUILD_FLASH_BYTES 4194304', header)
            # Reusing an output directory must also replace stale capability flags.
            write_capabilities(sketch/'build_capabilities.hpp', partition_capabilities(ROOT/'G5500_Fork/partitions_esp32_8mb.csv', '8MB'))
            header = (sketch/'build_capabilities.hpp').read_text()
            self.assertIn('#define G5500_HAS_OTA_PARTITIONS 1', header)
            self.assertIn('#define G5500_BUILD_FLASH_BYTES 8388608', header)
