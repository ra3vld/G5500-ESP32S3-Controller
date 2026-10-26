"""Exercise real Git histories, release boundaries, build identity and source archives."""
import configparser
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

from tools.project_version import bump_level, generate, resolve

ROOT = Path(__file__).resolve().parents[1]


class ProjectVersionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'repo'
        self.root.mkdir()
        self.git('init', '-q')
        self.git('config', 'user.name', 'Version test')
        self.git('config', 'user.email', 'version@example.invalid')
        (self.root / 'file').write_text('baseline')
        self.git('add', 'file')
        self.git('commit', '-qm', 'baseline')
        anchor = self.git('rev-parse', 'HEAD')
        (self.root / 'G5500_Fork').mkdir()
        (self.root / 'G5500_Fork/project_version.hpp').write_text(
            f'#define PROJECT_BASE_VERSION "1.0.0"\n#define PROJECT_BASE_COMMIT "{anchor}"\n')
        (self.root / '.gitignore').write_text('G5500_Fork/build_identity.hpp\nartifacts/\n__pycache__/\n')
        self.git('add', '.')
        self.git('commit', '-qm', 'Initialize version tracking')
        self.git('tag', 'v1.0.0')

    def git(self, *args):
        return subprocess.check_output(['git', *args], cwd=self.root, text=True,
                                       stderr=subprocess.PIPE).strip()

    def commit(self, message):
        self.git('commit', '--allow-empty', '-qm', message)

    def test_releases_and_bump_reset(self):
        self.assertEqual(resolve(self.root)['version'], '1.0.0')
        self.commit('fix(web): correct display')
        self.assertTrue(resolve(self.root)['version'].startswith('1.0.1-dev.1+g'))
        self.commit('feat(api): add endpoint')
        self.assertTrue(resolve(self.root)['version'].startswith('1.1.0-dev.2+g'))
        self.commit('refactor(api)!: replace endpoint')
        self.assertTrue(resolve(self.root)['version'].startswith('2.0.0-dev.3+g'))
        self.git('tag', '-a', 'v2.0.0', '-m', 'release')
        self.assertEqual(resolve(self.root)['version'], '2.0.0')
        self.commit('fix: next correction')
        self.assertTrue(resolve(self.root)['version'].startswith('2.0.1-dev.1+g'))

    def test_breaking_footer_and_legacy_subjects(self):
        self.assertEqual(bump_level('feat(driver): extend output'), 2)
        self.assertEqual(bump_level('fix!: remove old setting'), 3)
        self.assertEqual(bump_level('refactor: simplify\n\nBREAKING CHANGE: old API removed'), 3)
        self.assertEqual(bump_level('refactor: simplify\n\nBREAKING-CHANGE: old API removed'), 3)
        self.assertEqual(bump_level('Fix web interface'), 1)
        self.assertEqual(bump_level('docs: describe feat: example'), 1)

    def test_only_reachable_valid_tags(self):
        self.git('tag', 'v01.2.3')
        self.git('tag', 'v99-not-a-release')
        self.git('checkout', '-qb', 'other')
        self.commit('feat!: incompatible branch')
        self.git('tag', 'v99.0.0')
        self.git('checkout', '-q', '-')
        self.commit('fix: current branch')
        self.assertTrue(resolve(self.root)['version'].startswith('1.0.1-dev.1+g'))

    def test_dirty_and_unchanged_header(self):
        clean = generate(self.root)
        header = self.root / 'G5500_Fork/build_identity.hpp'
        modified = header.stat().st_mtime_ns
        self.assertFalse(generate(self.root)['dirty'])
        self.assertEqual(header.stat().st_mtime_ns, modified)
        (self.root / 'file').write_text('modified')
        dirty = generate(self.root)
        self.assertTrue(dirty['dirty'])
        self.assertEqual(dirty['version'], '1.0.0+g' + clean['commit'] + '.dirty')
        self.git('checkout', '--', 'file')
        (self.root / 'new.cpp').write_text('// untracked code')
        self.assertTrue(resolve(self.root)['dirty'])

    def test_baseline_and_missing_history(self):
        self.git('tag', '-d', 'v1.0.0')
        self.assertTrue(resolve(self.root)['version'].startswith('1.0.1-dev.1+g'))
        header = self.root / 'G5500_Fork/project_version.hpp'
        header.write_text(header.read_text().replace(self.git('rev-list', '--max-parents=0', 'HEAD'), 'f' * 40))
        with self.assertRaisesRegex(ValueError, 'full history'):
            resolve(self.root)

    def test_archive_preserves_identity_and_never_uses_parent_git(self):
        identity = generate(self.root)
        archive = self.root / 'artifacts/export'
        shutil.copytree(self.root / 'G5500_Fork', archive / 'G5500_Fork')
        self.assertEqual(generate(archive), identity)
        (archive / 'G5500_Fork/build_identity.hpp').unlink()
        self.assertEqual(generate(archive)['version'], '1.0.0+unknown')

    def test_source_package_embeds_identity(self):
        (self.root / 'tools').mkdir()
        for name in ('project_version.py', 'package_release.py'):
            shutil.copy2(ROOT / 'tools' / name, self.root / 'tools' / name)
        self.git('add', 'tools')
        self.git('commit', '-qm', 'feat: package sources')
        expected = resolve(self.root)
        subprocess.run([sys.executable, str(self.root / 'tools/package_release.py')], check=True,
                       stdout=subprocess.PIPE)
        with zipfile.ZipFile(self.root / 'artifacts/g5500-controller-sources.zip') as archive:
            data = archive.read('g5500-controller/G5500_Fork/build_identity.hpp').decode()
            self.assertIn(json.dumps(expected['version']), data)
            self.assertIn(json.dumps(expected['commit']), data)
            self.assertIn('G5500_Fork/build_identity.hpp', archive.read('g5500-controller/SOURCE_SHA256SUMS').decode())

    def test_all_platformio_environments_keep_version_hook(self):
        config = configparser.ConfigParser(interpolation=configparser.ExtendedInterpolation())
        config.read(ROOT / 'platformio.ini')
        # Resolve the PlatformIO ${section.option} syntax, including inherited overrides.
        def scripts(section):
            value = config.get(section, 'extra_scripts', raw=True, fallback=None)
            if value is None:
                parent = config.get(section, 'extends', raw=True, fallback='env')
                return scripts(parent)
            return value.replace('${env.extra_scripts}', config.get('env', 'extra_scripts', raw=True))
        for section in config.sections():
            if section.startswith('env:'):
                self.assertIn('pre:tools/platformio_version.py', scripts(section), section)


if __name__ == '__main__':
    unittest.main()
