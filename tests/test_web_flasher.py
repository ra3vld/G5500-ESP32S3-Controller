"""Reject unsafe, stale, corrupt or incomplete firmware before publication."""
import copy
import configparser
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import web_flasher as wf
import web_flasher_spp as spp


class WebFlasherTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name) / 'firmware'
        self.directory.mkdir()
        self.profile = next(p for p in wf.profiles() if p['id'] == 'esp32_4mb')
        self.identity = dict(version='1.2.3', commit='abcdef0', dirty=False)
        binaries = {'bootloader.bin': (0x1000, b'boot' * 100),
                    'partitions.bin': (0x8000, struct.pack('<HBBII16sI', 0x50aa, 0, 0, 0x10000, 0x300000, b'factory', 0)),
                    'boot_app0.bin': (0xe000, b'\xff' * 8192),
                    'firmware.bin': (0x10000, b'firmware' * 100)}
        files = []
        for name, (offset, data) in binaries.items():
            path = self.directory / name
            path.write_bytes(data)
            files.append(dict(path=name, offset=offset, size=len(data), sha256=wf.sha256(path)))
        self.report = dict(schema=1, profile=self.profile['id'], identity=self.identity,
                           source_sha256='test-digest', flash_bytes=4194304, chip_family='ESP32',
                           capabilities=dict.fromkeys(wf.CAPABILITIES, False),
                           static_dram_bytes=1000, static_iram_bytes=2000, runtime_heap_bytes=None,
                           app_bytes=800, app_limit_bytes=0x300000, files=files)
        for service, flags in wf.SERVICES.items():
            self.report['capabilities'].update(dict.fromkeys(flags, self.profile['services'][service]))
        self.save()

    def save(self):
        (self.directory / 'build.json').write_text(json.dumps(self.report))

    def validate(self):
        return wf.validate_build(self.profile, self.directory, self.identity, 'test-digest')

    def test_profiles_reference_existing_environments_and_docs(self):
        profiles = wf.profiles()
        self.assertEqual(len(profiles), 71)
        self.assertEqual(len({p["base_id"] for p in profiles}), 16)
        self.assertEqual(len({p["id"] for p in profiles}), 71)
        self.assertEqual(len({p['board'] for p in profiles}), 10)

    def test_independent_service_combinations(self):
        profiles = wf.profiles()
        for base in {p['base_id'] for p in profiles}:
            variants = [p for p in profiles if p['base_id'] == base]
            available = variants[0]['available_services']
            self.assertEqual(len(variants), 2 ** sum(available.values()))
            self.assertEqual(len({tuple(p['services'].values()) for p in variants}), len(variants))
        spp = next(p for p in profiles if p['id'] == 'esp32_spp')
        self.assertEqual(spp['recipe'], 'esp32_spp')
        self.assertFalse(any(spp['services'].values()))

    def test_rejects_binary_with_wrong_services(self):
        self.report['capabilities']['MQTT_SUPPORT'] = False
        self.save()
        with self.assertRaisesRegex(ValueError, 'service selection mismatch'):
            self.validate()

    def test_board_photos_are_local_and_match_sources(self):
        photos = json.loads((wf.ROOT / 'web-flasher/photos.json').read_text())
        self.assertEqual(set(photos), {p['photo'] for p in wf.profiles()})
        for photo in photos.values():
            self.assertTrue(photo['source'].startswith('https://'))
            self.assertTrue(photo['original'].startswith('https://'))
            self.assertEqual(Path(photo['file']).name, photo['file'])
            self.assertEqual(wf.sha256(wf.ROOT / 'web-flasher/images' / photo['file']), photo['sha256'])

    def test_complete_image_set(self):
        self.assertEqual(self.validate()['app_limit_bytes'], 0x300000)

    def test_source_directories_are_never_deleted(self):
        for directory in (wf.ROOT, wf.ROOT / 'docs', wf.ROOT / 'web-flasher', wf.ROOT / '.git', Path(tempfile.gettempdir())):
            with self.subTest(directory=directory), self.assertRaises(ValueError):
                wf.assemble(directory, allow_missing=True)

    def test_parallel_profiles_have_separate_effective_sketches_and_caches(self):
        root = Path(self.temp.name) / 'project'
        source = root / 'G5500_Fork'
        source.mkdir(parents=True)
        (source / 'controller.ino').write_text('void setup() {}\nvoid loop() {}\n')
        (source / 'controller.ino.cpp').write_text('temporary Arduino output')
        (root / 'platformio.ini').write_text('[platformio]\nsrc_dir = G5500_Fork\n[env]\nextra_scripts = pre:version.py\n[env:first]\n[env:second]\n')
        artifacts = root / 'artifacts'
        def complete(command, **kwargs):
            name = command[command.index('-e') + 1].removeprefix('web_')
            (artifacts / name).mkdir(parents=True)
            (artifacts / name / 'build.json').write_text('{}')
        with patch.object(wf, 'ROOT', root), patch.object(wf, 'ARTIFACTS', artifacts), \
             patch.object(wf, 'source_digest', return_value='digest'), patch.object(wf.subprocess, 'run', side_effect=complete):
            for name in ('first', 'second'):
                wf.build({'id':name}, 'pio', 2)
                config = configparser.ConfigParser(interpolation=None)
                config.read(root / 'build/web-flasher-config' / (name + '.ini'))
                self.assertNotIn('extra_configs', config['platformio'])
                sketch = Path(config['platformio']['src_dir'])
                self.assertIn(name, sketch.parts)
                self.assertEqual((sketch / 'controller.ino').read_text(), (source / 'controller.ino').read_text())
                self.assertFalse((sketch / 'controller.ino.cpp').exists())
                self.assertIn(name, Path(config['platformio']['build_dir']).parts)
            self.assertTrue((source / 'controller.ino.cpp').exists())

    def test_wrong_target_and_stale_revision(self):
        for key, wrong in [('profile', 'other'), ('chip_family', 'ESP32-S3'),
                           ('source_sha256', 'stale'), ('flash_bytes', 16777216),
                           ('identity', dict(self.identity, commit='aaaaaaa'))]:
            original = self.report[key]
            self.report[key] = wrong
            self.save()
            with self.subTest(key=key), self.assertRaises(ValueError): self.validate()
            self.report[key] = original

    def test_corruption_and_missing_image(self):
        path = self.directory / 'firmware.bin'
        data = path.read_bytes()
        path.write_bytes(b'x' * len(data))
        with self.assertRaises(ValueError): self.validate()
        path.unlink()
        with self.assertRaises(FileNotFoundError): self.validate()

    def test_nvs_overlap_wrong_offset_and_missing_bootloader(self):
        original = copy.deepcopy(self.report['files'])
        for offset in (0x9000, 0x8fff, 0xffff, 0x400000):
            self.report['files'] = copy.deepcopy(original)
            self.report['files'][0]['offset'] = offset
            self.save()
            with self.subTest(offset=offset), self.assertRaises(ValueError): self.validate()
        self.report['files'] = [item for item in original if item['path'] != 'bootloader.bin']
        self.save()
        with self.assertRaises(ValueError): self.validate()

    def test_oversized_application(self):
        path = self.directory / 'firmware.bin'
        path.write_bytes(b'x' * (0x300000 + 1))
        self.report['files'][-1].update(size=path.stat().st_size, sha256=wf.sha256(path))
        self.save()
        with self.assertRaises(ValueError): self.validate()

    def test_assembly_requires_all_builds_and_never_publishes_holes(self):
        artifacts = Path(self.temp.name) / 'artifacts'
        target = artifacts / self.profile['id']
        target.parent.mkdir()
        self.directory.rename(target)
        self.directory = target
        output = Path(self.temp.name) / 'site'
        with patch.object(wf, 'ARTIFACTS', artifacts), patch.object(wf, 'resolve', return_value=self.identity), \
             patch.object(wf, 'source_digest', return_value='test-digest'):
            with self.assertRaises(ValueError): wf.assemble(output)
            catalog = wf.assemble(output, allow_missing=True)
            self.assertEqual(sum(bool(p['build']) for p in catalog['profiles']), 1)
            profile = next(p for p in catalog['profiles'] if p['build'])
            manifest = json.loads((output / profile['artifact_base'] / 'manifest.json').read_text())
            self.assertTrue(manifest['new_install_prompt_erase'])
            self.assertEqual(manifest['new_install_improv_wait_time'], 0)
            self.assertEqual(manifest['builds'][0]['parts'][-1], {'path':'firmware.bin','offset':65536})
            self.assertNotIn('merged', json.dumps(manifest))
            self.assertTrue((output / profile['artifact_base'] / 'usb-firmware.zip').is_file())
            self.assertFalse((output / 'local-notes').exists())


class SppSdkTests(unittest.TestCase):
    def test_required_sdk_rejects_ble_wrong_target_and_single_host_slot(self):
        defaults = (wf.ROOT / 'tools/profiles/sdkconfig_esp32_spp.defaults').read_text()
        with tempfile.TemporaryDirectory() as temp:
            packages = Path(temp)
            package = packages / 'framework-arduinoespressif32-libs'
            header = package / 'esp32/dio_qspi/include/sdkconfig.h'
            header.parent.mkdir(parents=True)
            (package / 'package.json').write_text(json.dumps({'version':'5.5.5+sha.b774170ff46'}))
            values = {'CONFIG_IDF_TARGET_ESP32':'1'}
            for line in defaults.splitlines():
                key, value = line.split('=', 1)
                if value != 'n': values[key] = '1' if value == 'y' else value
            original = ''.join(f'#define {key} {value}\n' for key, value in values.items())
            header.write_text(original)
            self.assertTrue(spp.sdk_matches(packages, defaults))
            for invalid in (original + '#define CONFIG_BT_BLE_ENABLED 1\n',
                            original.replace('CONFIG_IDF_TARGET_ESP32 1', 'CONFIG_IDF_TARGET_ESP32S2 1'),
                            original.replace('CONFIG_BT_ACL_CONNECTIONS 2', 'CONFIG_BT_ACL_CONNECTIONS 1')):
                header.write_text(invalid)
                self.assertFalse(spp.sdk_matches(packages, defaults))
            (package / 'sdkconfig').write_text('CONFIG_IDF_TARGET_ESP32=y\n')
            project = packages / 'project'
            project.mkdir()
            spp.seed_cached_sdk(project, packages)
            self.assertTrue((project / 'sdkconfig.defaults').read_text().startswith('# TASMOTA__db37e978663de569\n'))


if __name__ == '__main__':
    unittest.main()
