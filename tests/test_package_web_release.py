"""A release must never contain a dirty version or an ELF from another build."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from package_web_release import check_tag, matching_elf, write_ota_assets


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def git(self, *args):
        return subprocess.check_output(['git', *args], cwd=self.root, text=True,
                                       stderr=subprocess.PIPE).strip()

    def repository(self):
        self.git('init', '-q')
        self.git('config', 'user.name', 'Release Test')
        self.git('config', 'user.email', 'release-test@example.invalid')
        source = self.root / 'G5500_Fork'
        source.mkdir()
        header = source / 'project_version.hpp'
        header.write_text('#define PROJECT_BASE_VERSION "1.0.0"\n'
                          '#define PROJECT_BASE_COMMIT "' + '0' * 40 + '"\n')
        self.git('add', 'G5500_Fork/project_version.hpp')
        self.git('commit', '-qm', 'feat: release test')
        self.git('tag', '-a', 'v1.1.0', '-m', 'v1.1.0')

    def test_clean_exact_tag_is_required(self):
        self.repository()
        release = check_tag('v1.1.0', self.root)
        self.assertEqual(release['identity']['version'], '1.1.0')
        self.assertFalse(release['identity']['dirty'])
        (self.root / 'untracked.txt').write_text('uncommitted')
        with self.assertRaisesRegex(ValueError, 'clean checkout'):
            check_tag('v1.1.0', self.root)

    def test_tag_on_another_commit_is_rejected(self):
        self.repository()
        self.git('commit', '--allow-empty', '-qm', 'fix: next commit')
        with self.assertRaisesRegex(ValueError, 'does not point to HEAD'):
            check_tag('v1.1.0', self.root)

    def test_invalid_tags_are_rejected(self):
        for tag in ['1.1.0', 'v01.1.0', 'v1.1.0-rc1', 'v1.1.0+dirty', '--help']:
            with self.subTest(tag=tag), self.assertRaisesRegex(ValueError, 'vMAJOR'):
                check_tag(tag, self.root)

    def test_missing_corrupt_and_symlinked_elf_are_rejected(self):
        profile = dict(id='esp32', build=dict(elf_sha256=hashlib.sha256(b'correct ELF').hexdigest()))
        directory = self.root / 'esp32'
        directory.mkdir()
        elf = directory / 'firmware.elf'
        with self.assertRaisesRegex(ValueError, 'Missing or mismatched ELF'):
            matching_elf(profile, self.root)
        elf.write_bytes(b'wrong ELF')
        with self.assertRaisesRegex(ValueError, 'Missing or mismatched ELF'):
            matching_elf(profile, self.root)
        elf.write_bytes(b'correct ELF')
        self.assertEqual(matching_elf(profile, self.root), elf)
        elf.unlink()
        target = self.root / 'other.elf'
        target.write_bytes(b'correct ELF')
        elf.symlink_to(target)
        with self.assertRaisesRegex(ValueError, 'Missing or mismatched ELF'):
            matching_elf(profile, self.root)


class OtaReleaseTests(unittest.TestCase):
    def test_matching_application_manifest_and_single_partition_exclusion(self):
        from web_flasher import profiles
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            site, output = root / 'site', root / 'output'
            output.mkdir()
            items = []
            for ident in ('esp32s3_n16r8_headless', 'esp32_4mb'):
                profile = next(p for p in profiles() if p['id'] == ident).copy()
                folder = site / ident
                folder.mkdir(parents=True)
                image = b'ESP32 application ' + ident.encode()
                (folder / 'firmware.bin').write_bytes(image)
                profile.update(artifact_base=ident, build=dict(
                    capabilities=dict(OTA_SUPPORT=True), app_bytes=len(image), app_limit_bytes=6553600,
                    files=[dict(path='firmware.bin', sha256=hashlib.sha256(image).hexdigest())]))
                items.append(profile)
            catalog = dict(identity=dict(version='2.0.0'), profiles=items)
            names = write_ota_assets(site, output, catalog, 'v2.0.0')
            self.assertEqual(set(names), {'ota-esp32s3_n16r8_headless.bin', 'ota-esp32s3_n16r8_headless.json'})
            import json
            manifest = json.loads((output / names[1]).read_text())
            self.assertEqual(manifest['profile'], 'esp32s3_n16r8_headless')
            self.assertEqual(manifest['version'], '2.0.0')
            self.assertEqual(manifest['size'], len((output / names[0]).read_bytes()))
            self.assertEqual(manifest['sha256'], hashlib.sha256((output / names[0]).read_bytes()).hexdigest())
            self.assertTrue(manifest['url'].endswith('/v2.0.0/' + names[0]))
            (site / items[0]['id'] / 'firmware.bin').write_bytes(b'wrong bytes')
            with self.assertRaisesRegex(ValueError, 'Mismatched OTA'):
                write_ota_assets(site, output, catalog, 'v2.0.0')


if __name__ == '__main__':
    unittest.main()
