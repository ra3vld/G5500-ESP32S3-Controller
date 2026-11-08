"""Prepare the pinned, isolated Classic-SPP SDK used by the web catalog."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
from concurrent.futures import ThreadPoolExecutor
from build_s2_sdk import BUILDER, TINYUSB, copy_changed, fetch_sources, sync_source, write_changed

ROOT = Path(__file__).resolve().parents[1]


def sdk_defaults():
    integrated = ROOT / 'tools/sdkconfig_esp32_spp.defaults'
    return (integrated if (ROOT / 'platformio/esp32_spp.ini').is_file()
            else ROOT / 'tools/profiles/sdkconfig_esp32_spp.defaults').read_text()


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


def seed_cached_sdk(project, packages):
    defaults = sdk_defaults()
    checksum = hashlib.md5((defaults.strip() + 'esp32|False||').encode()).hexdigest()[:16]
    current = (packages / 'framework-arduinoespressif32-libs/sdkconfig').read_text()
    write_changed(project / 'sdkconfig.defaults', '# TASMOTA__' + checksum + '\n' + current)


def prepare_sdk(pio, jobs, sketch):
    integrated = (ROOT / 'platformio/esp32_spp.ini').is_file()
    packages = ROOT / ('.pio/packages-esp32-spp-55.03.312-1' if integrated
                       else '.pio/packages-esp32-spp')
    defaults = sdk_defaults()
    if sdk_matches(packages, defaults):
        return packages
    project = ROOT / 'build/web-flasher-spp-sdk'
    project.mkdir(parents=True, exist_ok=True)
    sync_source(sketch, project / 'G5500_Fork')
    sync_source(ROOT / 'tools', project / 'tools')
    if integrated:
        sync_source(ROOT / 'platformio', project / 'platformio')
    write_changed(project / 'base.ini', (ROOT / 'platformio.ini').read_text() +
                  ('' if integrated else '\n' + (ROOT / 'tools/profiles/esp32_spp.ini').read_text()))
    write_changed(project / 'platformio.ini',
                  '[platformio]\nextra_configs = base.ini' +
                  (', platformio/esp32_spp.ini' if integrated else '') + '\nsrc_dir = G5500_Fork\n'
                  '[env:spp_sdk]\nextends = env:esp32_spp\ncustom_sdkconfig =\n' +
                  ''.join(' ' + line + '\n' for line in defaults.splitlines()))
    sources = project / 'vendor'
    sources.mkdir(exist_ok=True)
    with ThreadPoolExecutor(2) as pool:
        builder = pool.submit(fetch_sources, 'espressif/esp32-arduino-lib-builder',
                              BUILDER, sources / ('lib-builder-' + BUILDER))
        tinyusb = pool.submit(fetch_sources, 'hathach/tinyusb',
                              TINYUSB, sources / ('tinyusb-' + TINYUSB))
        builder, tinyusb = builder.result(), tinyusb.result()
    component = project / 'components/arduino_tinyusb'
    shutil.copytree(builder / 'components/arduino_tinyusb', component,
                    dirs_exist_ok=True, copy_function=copy_changed)
    shutil.copytree(tinyusb, component / 'tinyusb', dirs_exist_ok=True, copy_function=copy_changed)
    write_changed(project / 'CMakeLists.txt', 'cmake_minimum_required(VERSION 3.16.0)\n'
                  'include($ENV{IDF_PATH}/tools/cmake/project.cmake)\nproject(g5500_spp_sdk)\n')
    environment = os.environ.copy()
    for name in ('PLATFORMIO_BUILD_FLAGS', 'PLATFORMIO_BUILD_DIR'):
        environment.pop(name, None)
    environment['PLATFORMIO_PACKAGES_DIR'] = str(packages)
    subprocess.run([pio, 'run', '-d', str(project), '-e', 'spp_sdk', '-j', str(jobs)],
                   env=environment, check=True)
    if not sdk_matches(packages, defaults):
        raise ValueError('Classic SPP SDK does not match the pinned configuration')
    return packages
