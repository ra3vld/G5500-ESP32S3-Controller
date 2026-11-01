import configparser
import importlib.util
import json
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
spec = importlib.util.spec_from_file_location('build_esp32_spp', ROOT / 'tools/build_esp32_spp.py')
sdk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sdk)


class CompactSdkTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.packages = Path(self.temp.name)
        self.package = self.packages / 'framework-arduinoespressif32-libs'
        self.header = self.package / 'esp32/dio_qspi/include/sdkconfig.h'
        self.header.parent.mkdir(parents=True)
        (self.package / 'package.json').write_text(json.dumps({'version': '5.5.5+sha.b774170ff46'}))
        self.defaults = (ROOT / 'tools/sdkconfig_esp32_spp.defaults').read_text()
        values = {'CONFIG_IDF_TARGET_ESP32': '1'}
        for line in self.defaults.splitlines():
            name, value = line.split('=', 1)
            if value != 'n':
                values[name] = '1' if value == 'y' else value
        self.header.write_text(''.join(f'#define {k} {v}\n' for k, v in values.items()))
        (self.package / 'sdkconfig').write_text('CONFIG_IDF_TARGET_ESP32=y\n')

    def test_accepts_required_sdk_and_rejects_one_shot_server(self):
        self.assertTrue(sdk.sdk_matches(self.packages, self.defaults))
        self.header.write_text(self.header.read_text().replace('CONFIG_BT_ACL_CONNECTIONS 2',
                                                            'CONFIG_BT_ACL_CONNECTIONS 1'))
        self.assertFalse(sdk.sdk_matches(self.packages, self.defaults))

    def test_rejects_ble_and_wrong_target(self):
        original = self.header.read_text()
        self.header.write_text(original + '#define CONFIG_BT_BLE_ENABLED 1\n')
        self.assertFalse(sdk.sdk_matches(self.packages, self.defaults))
        self.header.write_text(original.replace('CONFIG_IDF_TARGET_ESP32 1', 'CONFIG_IDF_TARGET_ESP32S2 1'))
        self.assertFalse(sdk.sdk_matches(self.packages, self.defaults))

    def test_cached_checksum_and_hud_profile(self):
        project = self.packages / 'project'
        project.mkdir()
        sdk.seed_cached_sdk(project, self.packages, self.defaults)
        self.assertTrue((project / 'sdkconfig.defaults').read_text().startswith('# TASMOTA__db37e978663de569\n'))
        self.assertIn('extends = env:esp32_spp\n', sdk.configuration(self.defaults))
        self.assertIn('extends = env:esp32_spp_hud\n', sdk.configuration(self.defaults, True))


class ProfileIsolationTests(unittest.TestCase):
    def test_sdk_defaults_and_standalone_profile_are_shared(self):
        config = configparser.ConfigParser(interpolation=None)
        config.read([ROOT / 'platformio.ini', ROOT / 'platformio/esp32_spp.ini'])
        self.assertEqual(config.get('platformio', 'default_envs'), 'esp32s3_n16r8')
        self.assertIn('platformio/esp32_spp.ini', config.get('platformio', 'extra_configs'))
        self.assertEqual(config.get('esp32_spp', 'custom_sdkconfig').strip(),
                         (ROOT / 'tools/sdkconfig_esp32_spp.defaults').read_text().strip())
        for hud in (False, True):
            wrapper = sdk.configuration('', hud)
            self.assertIn('base.ini, platformio/esp32_spp.ini', wrapper)

    def test_engine_and_web_selection(self):
        for compact in (False, True):
            with self.subTest(compact=compact):
                # The full layout and large runtime state must remain available
                # to ordinary builds; compact state and pages are profile-only.
                code = '#include "control.hpp"\n#include "web_assets.hpp"\n'
                if compact:
                    code += ('static_assert(sizeof(forkrot::AutoTuneState) < 64);\n'
                             'static_assert(sizeof(forkrot::MotionFeedback) < 256);\n'
                             'static_assert(WEB_PAGE_RAW_SIZE < 20000);\n'
                             'static_assert(ESSENTIAL_SETTINGS_PAGE_RAW_SIZE > 0);\n')
                else:
                    code += ('static_assert(sizeof(forkrot::AutoTuneState) > 256);\n'
                             'static_assert(sizeof(forkrot::MotionFeedback) > 900);\n'
                             'static_assert(WEB_PAGE_RAW_SIZE > 100000);\n')
                result = subprocess.run(['g++', '-std=c++17', '-fsyntax-only', '-I', str(ROOT / 'G5500_Fork'),
                                         '-DBOARD_PROFILE=4', f'-DESP32_SPP_PROFILE={int(compact)}',
                                         '-x', 'c++', '-'], input=code, text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
