#!/usr/bin/env python3
"""Build the 4 MB headless S2 with an isolated, smaller Arduino SDK. Never flash."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
BUILDER = 'ee57070cc8571c488a64b8ef028684acdc6ff5e2'
TINYUSB = '53f8c53c2cbd73a91a172f1ae35e9abc00eb5075'
FEATURES = ('MQTT_SUPPORT', 'HOME_ASSISTANT_DISCOVERY_SUPPORT', 'N1MM_SUPPORT',
            'PSTROTATOR_SUPPORT', 'ROTCTLD_SUPPORT', 'RSSI_HISTORY_SUPPORT',
            'LOG_HISTORY_SUPPORT', 'NTP_SUPPORT')


def write_changed(path, text):
    if not path.exists() or path.read_text() != text:
        path.write_text(text)


def copy_changed(source, target):
    target = Path(target)
    if not target.exists() or Path(source).read_bytes() != target.read_bytes():
        shutil.copy2(source, target)
    return str(target)


def sync_source(source, target):
    target.mkdir(parents=True, exist_ok=True)
    names = set()
    for item in source.iterdir():
        if item.name == '__pycache__' or item.name.endswith(('.pyc', '.orig', '.ino.cpp')):
            continue
        names.add(item.name)
        dest = target / item.name
        if item.is_dir():
            sync_source(item, dest)
        else:
            copy_changed(item, dest)
    for item in target.iterdir():
        if item.name not in names:
            if item.is_dir():
                shutil.rmtree(item)
            else:
                item.unlink()


def fetch_sources(repo, revision, target):
    if target.exists():
        return target
    url = f'https://codeload.github.com/{repo}/tar.gz/{revision}'
    with urllib.request.urlopen(url, timeout=60) as response:
        archive = response.read()
    temporary = target.with_name(target.name + '.download')
    if temporary.exists():
        shutil.rmtree(temporary)
    temporary.mkdir(parents=True)
    with tarfile.open(fileobj=io.BytesIO(archive), mode='r:gz') as package:
        package.extractall(temporary, filter='data')
    roots = list(temporary.iterdir())
    if len(roots) != 1 or not roots[0].is_dir():
        raise RuntimeError(f'Unexpected archive layout: {url}')
    roots[0].rename(target)
    temporary.rmdir()
    return target


def sdk_matches(packages, defaults):
    package = packages / 'framework-arduinoespressif32-libs'
    try:
        version = json.loads((package / 'package.json').read_text())['version']
        current = (package / 'sdkconfig').read_text()
    except (OSError, ValueError, KeyError):
        return False
    if version != '5.5.5+sha.b774170ff46' or 'CONFIG_IDF_TARGET_ESP32S2=y' not in current:
        return False
    values = dict(re.findall(r'^(CONFIG_\w+)=(.*)$', current, re.M))
    values.update({name: 'n' for name in re.findall(r'^# (CONFIG_\w+) is not set$', current, re.M)})
    for line in defaults.splitlines():
        if not line.startswith('CONFIG_'):
            continue
        name, value = line.split('=', 1)
        # IDF hides the static TX cache setting in the dynamic-buffer profile.
        if name == 'CONFIG_ESP_WIFI_CACHE_TX_BUFFER_NUM' and values.get('CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER') == 'y':
            continue
        if values.get(name) != value:
            return False
    return True


def configuration(defaults, bootstrap):
    result = ('[platformio]\nextra_configs = base.ini\nsrc_dir = G5500_Fork\n'
              '[env:s2_sdk]\nextends = env:esp32s2_4mb_ota_compact\n')
    if not bootstrap:
        # Reuse only the matching custom IDF build, rather than replacing it
        # when a new Arduino release changes the precompiled archive URL.
        result += 'platform_packages = framework-arduinoespressif32-libs @ 5.5.5+sha.b774170ff46\n'
    if bootstrap:
        # HybridCompile builds an SDK using a dummy sketch, then an intermediate
        # application. Neither uses LTO or becomes a deliverable flash image.
        result += ('board_upload.maximum_size = 3145728\n'
                   'extra_scripts = pre:tools/platformio_version.py\nbuild_flags =\n'
                   ' -D BOARD_PROFILE=BOARD_S2_LEGACY\n -D ARDUINO_USB_MODE=0\n'
                   ' -D ARDUINO_USB_CDC_ON_BOOT=1\n -D ARDUINO_OTA_SUPPORT=0\n'
                   ' -D WEB_LIVE_SUPPORT=0\n')
    else:
        result += 'build_flags =\n ${env:esp32s2_4mb_ota_compact.build_flags}\n'
    result += ''.join(f' -D {feature}=0\n' for feature in FEATURES)
    return result + 'custom_sdkconfig =\n' + ''.join(' ' + line + '\n' for line in defaults.splitlines())


def seed_cached_sdk(project, packages, defaults):
    # HybridCompile also fingerprints board memory settings. Saola-1 has no
    # PSRAM or memory overrides; call only after validating the custom SDK.
    checksum = hashlib.md5((defaults.strip() + 'esp32s2|False||').encode()).hexdigest()[:16]
    current = (packages / 'framework-arduinoespressif32-libs/sdkconfig').read_text()
    write_changed(project / 'sdkconfig.defaults', '# TASMOTA__' + checksum + '\n' + current)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pio', default=shutil.which('pio') or 'pio')
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--project-dir', type=Path, default=ROOT / 'build/s2-sdk-project')
    parser.add_argument('--packages-dir', type=Path, default=ROOT / '.pio/packages-s2-sdk-55.03.312-1')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/s2-sdk-firmware')
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    project, packages, output = (path.resolve() for path in
                                 (args.project_dir, args.packages_dir, args.output))
    marker = project / '.g5500-s2-sdk'
    if project.exists() and any(project.iterdir()) and not marker.exists():
        parser.error('--project-dir must be empty or created by this tool')
    project.mkdir(parents=True, exist_ok=True)
    marker.touch()
    for name in ('G5500_Fork', 'tools'):
        sync_source(ROOT / name, project / name)
    copy_changed(ROOT / 'platformio.ini', project / 'base.ini')
    subprocess.run([sys.executable, str(ROOT / 'tools/project_version.py'), '--output',
                    str(project / 'G5500_Fork/build_identity.hpp')], check=True)
    defaults = (ROOT / 'tools/sdkconfig_s2.defaults').read_text()
    sources = project / 'vendor'
    sources.mkdir(exist_ok=True)
    with ThreadPoolExecutor(2) as executor:
        builder = executor.submit(fetch_sources, 'espressif/esp32-arduino-lib-builder', BUILDER,
                                  sources / ('lib-builder-' + BUILDER))
        tinyusb = executor.submit(fetch_sources, 'hathach/tinyusb', TINYUSB,
                                  sources / ('tinyusb-' + TINYUSB))
        builder, tinyusb = builder.result(), tinyusb.result()
    component = project / 'components/arduino_tinyusb'
    shutil.copytree(builder / 'components/arduino_tinyusb', component,
                    dirs_exist_ok=True, copy_function=copy_changed)
    shutil.copytree(tinyusb, component / 'tinyusb', dirs_exist_ok=True, copy_function=copy_changed)
    write_changed(project / 'CMakeLists.txt', 'cmake_minimum_required(VERSION 3.16.0)\n'
                  'include($ENV{IDF_PATH}/tools/cmake/project.cmake)\nproject(g5500_s2_sdk)\n')
    environment = os.environ.copy()
    for name in ('PLATFORMIO_BUILD_FLAGS', 'PLATFORMIO_BUILD_DIR'):
        environment.pop(name, None)
    environment['PLATFORMIO_PACKAGES_DIR'] = str(packages)
    command = [args.pio, 'run', '-d', str(project), '-e', 's2_sdk', '-j', str(args.jobs)]
    if not sdk_matches(packages, defaults):
        print('Building compact S2 SDK without LTO', flush=True)
        write_changed(project / 'platformio.ini', configuration(defaults, True))
        subprocess.run(command, env=environment, check=True)
    if not sdk_matches(packages, defaults):
        raise RuntimeError('Built SDK does not match the required compact S2 configuration')
    seed_cached_sdk(project, packages, defaults)
    print('Building final S2 application with LTO and OTA size checks', flush=True)
    write_changed(project / 'platformio.ini', configuration(defaults, False))
    subprocess.run(command, env=environment, check=True)
    build = project / '.pio/build/s2_sdk'
    subprocess.run([sys.executable, str(ROOT / 'tools/check_firmware_size.py'),
                    str(build / 'firmware.bin'), str(build / 'partitions.bin')], check=True)
    output.mkdir(parents=True, exist_ok=True)
    manifest = {'sdk': dict(builder=BUILDER, tinyusb=TINYUSB), 'disabledFeatures': list(FEATURES),
                'sdkconfigSha256': hashlib.sha256((packages / 'framework-arduinoespressif32-libs/sdkconfig').read_bytes()).hexdigest(),
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
