"""Real archive round trips: interface, 26 USB kits and ELF stay independently bound."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
import warnings
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from package_web_release import write_archives
from update_web_flasher_site import extract_release, restore_firmware
from web_flasher import profiles, sha256


class SplitReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.site, self.elfs, self.release = [self.root / name for name in ('site', 'elfs', 'release')]
        self.site.mkdir()
        self.identity = dict(version='1.2.3', commit='abcdef0', dirty=False)
        self.digest = 'ab' * 32
        items = []
        for source in profiles():
            ident = source['id']
            base = 'firmware/abcdef0-' + self.digest[:16] + '/' + ident
            folder = self.site / base
            folder.mkdir(parents=True)
            elf = self.elfs / ident / 'firmware.elf'
            elf.parent.mkdir(parents=True)
            elf.write_bytes(b'ELF-' + ident.encode())
            binary = b'BIN-' + ident.encode()
            (folder / 'firmware.bin').write_bytes(binary)
            for name, content in {'manifest.json': '{}', 'build.json': '{}', 'SHA256SUMS': hashlib.sha256(binary).hexdigest() + '  firmware.bin\n'}.items():
                (folder / name).write_text(content)
            with zipfile.ZipFile(folder / 'usb-firmware.zip', 'w') as z:
                for name in ('firmware.bin', 'manifest.json', 'build.json', 'SHA256SUMS'):
                    z.write(folder / name, name)
            items.append(dict(id=ident, artifact_base=base, build=dict(elf_sha256=sha256(elf), files=[dict(path='firmware.bin')])) )
        with zipfile.ZipFile(self.site / 'source-code.zip', 'w') as z:
            z.writestr('main.cpp', 'exact release source')
        self.catalog = dict(identity=self.identity, profiles=items, source_archive_sha256=sha256(self.site / 'source-code.zip'))
        (self.site / 'catalog.json').write_text(json.dumps(self.catalog))
        (self.site / 'index.html').write_text('<html>interface</html>')
        (self.site / 'documentation').mkdir()
        (self.site / 'documentation/old.html').write_text('old embedded docs must be excluded')
        self.archives = write_archives(self.site, self.elfs, self.release, self.catalog)
        self.manifest = dict(schema=2, tag='v1.2.3', identity=self.identity, source_sha256=self.digest, archives=self.archives)
        self.save_manifest()
        self.output = self.root / 'assembled'
        extract_release(self.release / 'web-flasher-site.zip', self.output)

    def checksums(self):
        self.sums = self.release / 'SHA256SUMS'
        self.sums.write_text(''.join(sha256(p) + '  ' + p.name + '\n' for p in sorted(self.release.iterdir()) if p.name != 'SHA256SUMS'))

    def save_manifest(self):
        (self.release / 'release-manifest.json').write_text(json.dumps(self.manifest))
        self.checksums()

    def restore(self, tag='v1.2.3'):
        return restore_firmware(self.output, self.release, self.sums, tag)

    def test_separate_archives_and_exact_round_trip_of_all_26(self):
        self.assertEqual(len(self.archives['firmware']), 26)
        self.assertEqual(len(list(self.release.glob('*.zip'))), 29)
        with zipfile.ZipFile(self.release / 'web-flasher-site.zip') as z:
            self.assertEqual(set(z.namelist()), {'index.html', 'catalog.json'})
        with zipfile.ZipFile(self.release / 'diagnostic-elf.zip') as z:
            self.assertEqual(len(z.namelist()), 26)
            self.assertTrue(all(name.endswith('/firmware.elf') for name in z.namelist()))
        checked = self.restore()
        self.assertEqual(len(checked), 27)
        self.assertFalse((self.output / 'documentation').exists())
        for profile in self.catalog['profiles']:
            before, after = self.site / profile['artifact_base'], self.output / profile['artifact_base']
            for file in before.iterdir():
                self.assertEqual(file.read_bytes(), (after / file.name).read_bytes())
        self.assertEqual((self.output / 'source-code.zip').read_bytes(), (self.site / 'source-code.zip').read_bytes())
        self.assertEqual((self.output / 'catalog.json').read_bytes(), (self.site / 'catalog.json').read_bytes())

    def test_corrupt_or_missing_profile_archive_is_rejected(self):
        archive = self.release / next(iter(self.archives['firmware'].values()))
        archive.write_bytes(archive.read_bytes() + b'corruption')
        with self.assertRaisesRegex(ValueError, 'checksum'):
            self.restore()
        archive.unlink()
        with self.assertRaisesRegex(ValueError, 'checksum'):
            self.restore()

    def test_wrong_tag_and_identity_are_rejected(self):
        with self.assertRaisesRegex(ValueError, 'identity'):
            self.restore(tag='v9.9.9')
        self.manifest['identity'] = dict(self.identity, commit='badcafe')
        self.save_manifest()
        with self.assertRaisesRegex(ValueError, 'identity'):
            self.restore()

    def test_missing_or_extra_archive_mapping_is_rejected(self):
        self.manifest['archives']['firmware']['unknown'] = 'firmware-unknown.zip'
        self.save_manifest()
        with self.assertRaisesRegex(ValueError, 'archive set'):
            self.restore()

    def test_unsafe_target_path_is_rejected(self):
        self.catalog['profiles'][0]['artifact_base'] = '../outside'
        (self.output / 'catalog.json').write_text(json.dumps(self.catalog))
        with self.assertRaisesRegex(ValueError, 'artifact path'):
            self.restore()
        self.assertFalse((self.root / 'outside').exists())

    def test_archive_extra_path_and_duplicates_are_rejected(self):
        archive = self.release / next(iter(self.archives['firmware'].values()))
        original = archive.read_bytes()
        with zipfile.ZipFile(archive, 'a') as z:
            z.writestr('../outside', 'must never be extracted')
        self.checksums()
        with self.assertRaisesRegex(ValueError, 'contents'):
            self.restore()
        self.assertFalse((self.root / 'outside').exists())
        archive.write_bytes(original)
        with warnings.catch_warnings():
            warnings.simplefilter('ignore', UserWarning)
            with zipfile.ZipFile(archive, 'a') as z:
                z.writestr('firmware.bin', 'duplicate')
        self.checksums()
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            self.restore()

    def test_wrong_source_archive_is_rejected_even_with_valid_download_checksum(self):
        (self.release / 'source-code.zip').write_bytes(b'different source')
        self.checksums()
        with self.assertRaisesRegex(ValueError, 'source archive differs'):
            self.restore()

    def test_old_full_site_release_remains_supported(self):
        (self.release / 'release-manifest.json').unlink()
        (self.output / 'firmware').mkdir()
        self.assertEqual(self.restore(), {})

    def test_new_website_must_not_include_embedded_firmware_or_docs(self):
        (self.output / 'documentation').mkdir()
        with self.assertRaisesRegex(ValueError, 'must not embed'):
            self.restore()

    def test_packaging_does_not_mix_old_output_files(self):
        with self.assertRaisesRegex(ValueError, 'must be empty'):
            write_archives(self.site, self.elfs, self.release, self.catalog)


if __name__ == '__main__':
    unittest.main()
