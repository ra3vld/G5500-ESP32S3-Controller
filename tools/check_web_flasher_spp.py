#!/usr/bin/env python3
"""Run SPP motion, timeout, storage and HTTP regressions on its isolated sources."""
from pathlib import Path
import subprocess
import sys
from build_s2_sdk import sync_source

ROOT = Path(__file__).resolve().parents[1]


def main():
    project = ROOT / 'build/web-flasher-spp-tests'
    for name in ('G5500_Fork', 'tools', 'tests'):
        sync_source(ROOT / name, project / name)
    for name in ('esp32_spp.patch', 'esp32_spp_tests.patch'):
        subprocess.run(['git', 'apply', '--unsafe-paths', '-p1', '--directory=' + str(project),
                        str(ROOT / 'tools/profiles' / name)], check=True, cwd=ROOT)
    subprocess.run([sys.executable, str(project / 'tools/check_native.py'),
                    'direct_motion_tests', 'minimal_storage_tests', 'http_ingress_esp32_tests'],
                   check=True, cwd=project)


if __name__ == '__main__':
    main()
