"""Release reuse must preserve identity and reject corrupt or unsafe archives."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from check_web_flasher_catalog import released_identity
from update_web_flasher_site import extract_release, verify_archive


class SiteUpdateTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_archive_checksum_is_required(self):
        archive = self.root / 'site.zip'
        archive.write_bytes(b'release bytes')
        sums = self.root / 'SHA256SUMS'
        sums.write_text(hashlib.sha256(archive.read_bytes()).hexdigest() + '  site.zip\n')
        verify_archive(archive, sums)
        archive.write_bytes(b'other bytes')
        with self.assertRaisesRegex(ValueError, 'checksum'):
            verify_archive(archive, sums)
        sums.write_text('0' * 64 + '  unrelated.zip\n')
        with self.assertRaisesRegex(ValueError, 'checksum'):
            verify_archive(archive, sums)

    def test_extract_and_existing_output(self):
        archive = self.root / 'site.zip'
        with zipfile.ZipFile(archive, 'w') as package:
            package.writestr('firmware/board/firmware.bin', b'release image')
        output = self.root / 'site'
        extract_release(archive, output)
        self.assertEqual((output / 'firmware/board/firmware.bin').read_bytes(), b'release image')
        with self.assertRaisesRegex(ValueError, 'must not exist'):
            extract_release(archive, output)

    def test_archive_cannot_escape_or_create_symlinks(self):
        for name in ('../outside', '/outside', 'folder\\outside', 'link'):
            with self.subTest(name=name):
                archive = self.root / 'bad.zip'
                with zipfile.ZipFile(archive, 'w') as package:
                    entry = zipfile.ZipInfo(name)
                    if name == 'link':
                        entry.create_system = 3
                        entry.external_attr = 0o120777 << 16
                    package.writestr(entry, 'outside')
                with self.assertRaisesRegex(ValueError, 'Unsafe'):
                    extract_release(archive, self.root / 'output')
                self.assertFalse((self.root / 'output').exists())

    def test_tag_identity_is_not_current_head(self):
        def git(*args):
            return subprocess.check_output(['git', *args], cwd=self.root, text=True, stderr=subprocess.PIPE).strip()
        git('init', '-q')
        git('config', 'user.name', 'Release Test')
        git('config', 'user.email', 'test@example.invalid')
        git('commit', '--allow-empty', '-qm', 'feat: firmware')
        original = git('rev-parse', '--short=7', 'HEAD')
        git('tag', '-a', 'v1.2.3', '-m', 'release')
        git('commit', '--allow-empty', '-qm', 'fix: website')
        self.assertEqual(released_identity('v1.2.3', self.root),
                         dict(version='1.2.3', commit=original, dirty=False))
        self.assertNotEqual(original, git('rev-parse', '--short=7', 'HEAD'))
        for tag in ('main', '--help', 'v1.2.3-rc1', 'v01.2.3'):
            with self.assertRaisesRegex(ValueError, 'stable'):
                released_identity(tag, self.root)


if __name__ == '__main__':
    unittest.main()
