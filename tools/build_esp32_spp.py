#!/usr/bin/env python3
"""Build Classic ESP32 SPP firmware with its isolated compact Arduino SDK."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

from build_s2_sdk import (BUILDER, TINYUSB, copy_changed, fetch_sources,
                          sync_source, write_changed)

ROOT = Path(__file__).resolve().parents[1]


def sdk_matches(packages, defaults):
    package = packages / 'framework-arduinoespressif32-libs'
    try:
        version = json.loads((package / 'package.json').read_text())['version']
        header = (package / 'esp32/dio_qspi/include/sdkconfig.h').read_text()
    except (OSError, ValueError, KeyError):
        return False
    if version != '5.5.5+sha.b774170ff46':
        return False
    values = dict(re.findall(r'^#define (CONFIG_\w+)\s+([^\n]+)', header, re.M))
    if values.get('CONFIG_IDF_TARGET_ESP32') != '1':
        return False
    for line in defaults.splitlines():
        if not line.startswith('CONFIG_'):
            continue
        name, wanted = line.split('=', 1)
        if wanted == 'n':
            if name in values and values[name] != '0':
                return False
        elif values.get(name) != ('1' if wanted == 'y' else wanted):
            return False
    return True


def configuration(defaults, hud=False):
    profile = 'esp32_spp_hud' if hud else 'esp32_spp'
    return ('[platformio]\nextra_configs = base.ini\nsrc_dir = G5500_Fork\n'
            f'[env:esp32_spp_sdk]\nextends = env:{profile}\n'
            'custom_sdkconfig =\n' +
            ''.join(' ' + line + '\n' for line in defaults.splitlines()))


def seed_cached_sdk(project, packages, defaults):
    # The pinned pioarduino release includes the esp32dev PSRAM fingerprint in
    # its HybridCompile checksum. This board has no PSRAM/memory overrides.
    checksum = hashlib.md5((defaults.strip() + 'esp32|False||').encode()).hexdigest()[:16]
    current = (packages / 'framework-arduinoespressif32-libs/sdkconfig').read_text()
    write_changed(project / 'sdkconfig.defaults', '# TASMOTA__' + checksum + '\n' + current)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pio', default=shutil.which('pio') or 'pio')
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--hud', action='store_true', help='Enable the HUD UDP transport')
    parser.add_argument('--project-dir', type=Path, default=ROOT / 'build/esp32-spp-project')
    parser.add_argument('--packages-dir', type=Path, default=ROOT / '.pio/packages-esp32-spp')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/esp32-spp-firmware')
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    project, packages, output = (v.resolve() for v in
                                 (args.project_dir, args.packages_dir, args.output))
    marker = project / '.g5500-esp32-spp-sdk'
    if project.exists() and any(project.iterdir()) and not marker.exists():
        parser.error('--project-dir must be empty or created by this tool')
    project.mkdir(parents=True, exist_ok=True)
    marker.touch()
    for name in ('G5500_Fork', 'tools'):
        sync_source(ROOT / name, project / name)
    copy_changed(ROOT / 'platformio.ini', project / 'base.ini')
    subprocess.run([sys.executable, str(ROOT / 'tools/project_version.py'), '--output',
                    str(project / 'G5500_Fork/build_identity.hpp')], check=True)
    defaults = (ROOT / 'tools/sdkconfig_esp32_spp.defaults').read_text()
    write_changed(project / 'platformio.ini', configuration(defaults, args.hud))
    if sdk_matches(packages, defaults):
        seed_cached_sdk(project, packages, defaults)
    else:
        sources = project / 'vendor'
        with ThreadPoolExecutor(2) as pool:
            builder = pool.submit(fetch_sources, 'espressif/esp32-arduino-lib-builder',
                                  BUILDER, sources / ('lib-builder-' + BUILDER))
            tinyusb = pool.submit(fetch_sources, 'hathach/tinyusb',
                                  TINYUSB, sources / ('tinyusb-' + TINYUSB))
            builder, tinyusb = builder.result(), tinyusb.result()
        component = project / 'components/arduino_tinyusb'
        shutil.copytree(builder / 'components/arduino_tinyusb', component,
                        dirs_exist_ok=True, copy_function=copy_changed)
        shutil.copytree(tinyusb, component / 'tinyusb',
                        dirs_exist_ok=True, copy_function=copy_changed)
        write_changed(project / 'CMakeLists.txt',
                      'cmake_minimum_required(VERSION 3.16.0)\n'
                      'include($ENV{IDF_PATH}/tools/cmake/project.cmake)\n'
                      'project(g5500_esp32_spp_sdk)\n')
    environment = os.environ.copy()
    for name in ('PLATFORMIO_BUILD_FLAGS', 'PLATFORMIO_BUILD_DIR'):
        environment.pop(name, None)
    environment['PLATFORMIO_PACKAGES_DIR'] = str(packages)
    subprocess.run([args.pio, 'run', '-d', str(project), '-e', 'esp32_spp_sdk',
                    '-j', str(args.jobs)], env=environment, check=True)
    if not sdk_matches(packages, defaults):
        raise RuntimeError('Built SDK does not match the required Classic SPP configuration')
    build = project / '.pio/build/esp32_spp_sdk'
    subprocess.run([sys.executable, str(ROOT / 'tools/check_firmware_size.py'),
                    str(build / 'firmware.bin'), str(build / 'partitions.bin')], check=True)
    output.mkdir(parents=True, exist_ok=True)
    manifest = {'hud': args.hud, 'sdk': {'builder': BUILDER, 'tinyusb': TINYUSB},
                'sdkconfigSha256': hashlib.sha256((packages /
                    'framework-arduinoespressif32-libs/esp32/dio_qspi/include/sdkconfig.h').read_bytes()).hexdigest(),
                'files': {}}
    for name in ('firmware.bin', 'firmware.elf', 'partitions.bin', 'bootloader.bin', 'boot_app0.bin'):
        if not (build / name).exists():
            continue
        copy_changed(build / name, output / name)
        data = (output / name).read_bytes()
        manifest['files'][name] = {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Firmware ready: {output / "firmware.bin"}', flush=True)


if __name__ == '__main__':
    main()
