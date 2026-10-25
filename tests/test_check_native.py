"""Verify incremental native builds without running the firmware test matrix."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]


class NativeCheckTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='native check ')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        for directory in ['tools', 'tests', 'G5500_Fork']:
            (self.root / directory).mkdir()
        shutil.copy2(ROOT / 'tools/check_native.py', self.root / 'tools/check_native.py')
        (self.root / 'G5500_Fork/value.hpp').write_text('#define VALUE 1\n')
        (self.root / 'G5500_Fork/shared.cpp').write_text(
            '#include "value.hpp"\nint value() { return VALUE; }\n')
        for name in ['first_tests', 'second_tests']:
            (self.root / f'tests/{name}.cpp').write_text(
                'int value(); int main() { return value() == 1 ? 0 : 1; }\n')
        self.script = self.root / 'tools/build_native.sh'
        self.script.write_text(
            'CORE_SOURCES="G5500_Fork/shared.cpp"\n' + ''.join(
                '${CXX:-g++} -std=c++17 -O2 -I G5500_Fork $CORE_SOURCES '
                f'tests/{name}.cpp -o build/{name}\n'
                for name in ['first_tests', 'second_tests']))

    def run_check(self, *arguments, success=True):
        result = subprocess.run([sys.executable, str(self.root / 'tools/check_native.py'),
                                 *arguments], cwd=self.root, text=True, capture_output=True)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return result.stdout + result.stderr

    def test_shared_objects_and_warm_run_still_execute_tests(self):
        first = self.run_check('first_tests', 'tests/second_tests.cpp')
        self.assertEqual(first.count('CXX G5500_Fork/shared.cpp'), 1, first)
        warm = self.run_check('first_tests', 'second_tests')
        self.assertNotIn('CXX ', warm)
        self.assertNotIn('LINK ', warm)
        self.assertIn('first_tests:', warm)
        self.assertIn('second_tests:', warm)

    def test_header_and_source_invalidate_only_dependent_objects(self):
        self.run_check('first_tests', 'second_tests')
        time.sleep(0.02)
        header = self.root / 'G5500_Fork/value.hpp'
        header.write_text('#define VALUE 1\n// Updated header\n')
        output = self.run_check('first_tests', 'second_tests')
        self.assertEqual(output.count('CXX G5500_Fork/shared.cpp'), 1, output)
        self.assertNotIn('CXX tests/', output)
        self.assertEqual(output.count('LINK '), 2, output)
        time.sleep(0.02)
        source = self.root / 'tests/first_tests.cpp'
        source.write_text(source.read_text() + '// Updated test\n')
        output = self.run_check('first_tests', 'second_tests')
        self.assertIn('CXX tests/first_tests.cpp', output)
        self.assertNotIn('CXX G5500_Fork/', output)
        self.assertNotIn('LINK second_tests', output)

    def test_changed_flags_do_not_reuse_objects(self):
        self.run_check('first_tests')
        self.script.write_text(self.script.read_text().replace('-O2', '-O0'))
        output = self.run_check('first_tests')
        self.assertIn('CXX G5500_Fork/shared.cpp', output)
        self.assertIn('CXX tests/first_tests.cpp', output)

    def test_failed_test_is_never_cached_as_pass(self):
        self.run_check('first_tests')
        time.sleep(0.02)
        (self.root / 'G5500_Fork/value.hpp').write_text('#define VALUE 2\n')
        self.run_check('first_tests', success=False)
        output = self.run_check('first_tests', success=False)
        self.assertNotIn('CXX ', output)
        self.assertIn('Native check failed:', output)

    def test_compile_failure_does_not_execute_old_binary(self):
        source = self.root / 'tests/first_tests.cpp'
        source.write_text('#include <fstream>\nint main() { '
                          'std::ofstream("executed") << "yes"; }\n')
        self.run_check('first_tests')
        marker = self.root / 'executed'
        marker.unlink()
        time.sleep(0.02)
        source.write_text('invalid C++ source\n')
        self.run_check('first_tests', success=False)
        self.assertFalse(marker.exists())

    def test_no_implicit_full_run_and_no_shell_expansion(self):
        self.run_check(success=False)
        self.run_check('unknown_tests', success=False)
        self.script.write_text(self.script.read_text() +
                               '${CXX:-g++} -DVALUE=$(touch injected) tests/first_tests.cpp '
                               '-o build/injected_tests\n')
        output = self.run_check('--list')
        self.assertIn('first_tests', output)
        self.assertNotIn('injected_tests', output)
        self.assertFalse((self.root / 'injected').exists())
        self.assertFalse((self.root / 'build').exists())


if __name__ == '__main__':
    unittest.main()
