#!/usr/bin/env python3
"""Run SPP motion, timeout, storage and HTTP regressions on its isolated sources."""
from pathlib import Path
import subprocess
import sys
from build_s2_sdk import sync_source

ROOT = Path(__file__).resolve().parents[1]


def main():
    if (ROOT / 'platformio/esp32_spp.ini').is_file():
        subprocess.run([sys.executable, str(ROOT / 'tools/check_native.py'),
                        'esp32_spp_no_hud_api_tests', 'spp_manual_protocol_tests',
                        'direct_motion_tests'], check=True, cwd=ROOT)
        return
    project = ROOT / 'build/web-flasher-spp-tests'
    for name in ('G5500_Fork', 'tools', 'tests'):
        sync_source(ROOT / name, project / name)
    for name in ('esp32_spp.patch', 'esp32_spp_tests.patch'):
        subprocess.run(['git', 'apply', '--unsafe-paths', '-p1', '--directory=' + str(project),
                        str(ROOT / 'tools/profiles' / name)], check=True, cwd=ROOT)
    subprocess.run([sys.executable, str(project / 'tools/check_native.py'),
                    'direct_motion_tests', 'minimal_storage_tests', 'http_ingress_esp32_tests',
                    'esp32_spp_api_tests', 'manual_protocol_tests'],
                   check=True, cwd=project)


if __name__ == '__main__':
    main()
