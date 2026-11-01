import importlib.util
import json
from pathlib import Path
import sys
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


if __name__ == '__main__':
    unittest.main()
