"""A release must never contain a dirty version or an ELF from another build."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from package_web_release import check_tag, matching_elf


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


if __name__ == '__main__':
    unittest.main()
