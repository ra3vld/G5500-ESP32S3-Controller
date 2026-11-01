#!/usr/bin/env python3
"""Build fixed firmware profiles and assemble a self-contained static installer."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from itertools import product
import zipfile

from project_version import resolve
from check_firmware_size import check

ROOT = Path(__file__).resolve().parents[1]
PROFILES = ROOT / 'web-flasher/profiles.json'
ARTIFACTS = ROOT / 'artifacts/web-flasher'
CAPABILITIES = ['WEB_INTERFACE_ENABLED', 'BLUETOOTH_SUPPORT', 'DISPLAY_SUPPORT',
                'LOCAL_INPUT_SUPPORT', 'WIFI_SUPPORT', 'ETHERNET_SUPPORT',
                'WEB_LIVE_SUPPORT', 'WEB_PLAIN_HTML_SUPPORT', 'BLUETOOTH_SPP_SUPPORT',
                'GS232_SUPPORT', 'ROTCTLD_SUPPORT', 'N1MM_SUPPORT', 'PSTROTATOR_SUPPORT',
                'COMMAND_API_SUPPORT', 'HUD_SUPPORT', 'HUD_UDP_SUPPORT', 'HUD_UART_SUPPORT',
                'RS485_SUPPORT', 'MODBUS_RTU_SUPPORT', 'OTA_SUPPORT', 'ARDUINO_OTA_SUPPORT',
                'LOG_HISTORY_SUPPORT', 'RSSI_HISTORY_SUPPORT', 'RESET_HISTORY_SUPPORT',
                'NTP_SUPPORT', 'MQTT_SUPPORT', 'HOME_ASSISTANT_DISCOVERY_SUPPORT',
                'CAMERA_ENABLED', 'G5500_HAS_OTA_PARTITIONS']


SERVICES = {
    "mqtt": ("MQTT_SUPPORT", "HOME_ASSISTANT_DISCOVERY_SUPPORT"),
    "ethernet": ("ETHERNET_SUPPORT",),
    "rs485": ("RS485_SUPPORT", "MODBUS_RTU_SUPPORT"),
}


def profiles():
    import configparser
    config = configparser.ConfigParser(interpolation=None)
    config.read(ROOT / 'platformio.ini')
    config.read(ROOT / 'tools/profiles/esp32_spp.ini')
    data = json.loads(PROFILES.read_text())['profiles']
    ids = set()
    for profile in data:
        name = profile['id']
        if not re.fullmatch(r'[a-z0-9_]+', name) or name in ids or 'env:' + name not in config:
            raise ValueError('Invalid or duplicate profile: ' + name)
        if not (ROOT / profile['docs']).is_file():
            raise ValueError('Missing documentation: ' + profile['docs'])
        ids.add(name)
    variants = []
    for base in data:
        available = base['services']
        if set(available) != set(SERVICES) or any(type(v) is not bool for v in available.values()):
            raise ValueError('Invalid service selection: ' + base['id'])
        for values in product(*[(True, False) if available[k] else (False,) for k in SERVICES]):
            services = dict(zip(SERVICES, values))
            disabled = [k for k in SERVICES if available[k] and not services[k]]
            name = base['id'] + ('_no_' + '_'.join(disabled) if disabled else '')
            variants.append(dict(base, id=name, base_id=base['id'], services=services,
                                 available_services=available))
    return variants


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def source_digest():
    paths = sorted(p for p in (ROOT / 'G5500_Fork').rglob('*') if p.is_file()
                   and p.suffix in {'.h', '.hpp', '.cpp', '.c', '.ino', '.csv'}
                   and p.name != 'build_identity.hpp' and not p.name.endswith('.ino.cpp')
                   and 'build' not in p.relative_to(ROOT / 'G5500_Fork').parts)
    paths += [ROOT / name for name in ['platformio.ini',
              'tools/platformio_web_flasher.py', 'tools/web_flasher.py',
              'tools/platformio_version.py', 'tools/project_version.py',
              'tools/build_capabilities.py', 'tools/platformio_image_size.py',
              'tools/check_firmware_size.py', 'tools/platformio_lto.py',
              'tools/web_flasher_spp.py', 'tools/build_s2_sdk.py',
              'tools/profiles/esp32_spp.patch', 'tools/profiles/sdkconfig_esp32_spp.defaults',
              'tools/profiles/esp32_spp.ini']]
    digest = hashlib.sha256()
    digest.update(json.dumps([(p['id'], p['services']) for p in profiles()], sort_keys=True).encode())
    for path in paths:
        digest.update(path.relative_to(ROOT).as_posix().encode() + b'\0' + path.read_bytes() + b'\0')
    return digest.hexdigest()


def build(profile, pio, jobs):
    import configparser
    name = profile['id']
    base = profile.get('base_id', name)
    digest = source_digest()
    # Arduino generates and removes .ino.cpp beside the sketch. Each profile
    # therefore needs its own source directory as well as its own build cache.
    sketch = ROOT / 'build/web-flasher-source' / name / 'G5500_Fork'
    shutil.rmtree(sketch, ignore_errors=True)
    shutil.copytree(ROOT / 'G5500_Fork', sketch,
                    ignore=shutil.ignore_patterns('*.ino.cpp', 'build_identity.hpp', '__pycache__', 'build'))
    project = ROOT
    environment = os.environ.copy()
    for key in ("PLATFORMIO_BUILD_FLAGS", "PLATFORMIO_BUILD_DIR"):
        environment.pop(key, None)
    if profile.get('recipe') == 'esp32_spp':
        subprocess.run(['git', 'apply', '--unsafe-paths', '-p2', '--directory=' + str(sketch),
                        str(ROOT / 'tools/profiles/esp32_spp.patch')], cwd=ROOT, check=True)
        from web_flasher_spp import prepare_sdk, seed_cached_sdk
        project = sketch.parent
        shutil.copytree(ROOT / 'tools', project / 'tools', dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns('__pycache__'))
        from project_version import generate
        generate(ROOT, sketch / 'build_identity.hpp')
        packages = prepare_sdk(pio, jobs, sketch)
        environment['PLATFORMIO_PACKAGES_DIR'] = str(packages)
        seed_cached_sdk(project, packages)
    output = ROOT / 'build/web-flasher-config' / (name + '.ini')
    output.parent.mkdir(parents=True, exist_ok=True)
    # Flatten the base config: extra_configs would override src_dir again with
    # the shared sketch directory from platformio.ini.
    config = configparser.ConfigParser(interpolation=None)
    config.read(ROOT / 'platformio.ini')
    config.read(ROOT / 'tools/profiles/esp32_spp.ini')
    config['platformio']['src_dir'] = str(sketch)
    config['platformio']['build_dir'] = str(ROOT / '.pio/web-flasher' / name)
    config['env:web_' + name] = {
        'extends': 'env:' + base,
        'build_flags': '${env:' + base + '.build_flags}\n' + '\n'.join(
            '-D ' + flag + '=0' for service, flags in SERVICES.items()
            if service in profile.get('available_services', {}) and not profile['services'][service]
            for flag in flags),
        'extra_scripts': f'${{env:{base}.extra_scripts}}\npost:tools/platformio_web_flasher.py',
        'custom_web_flasher_profile': name,
        'custom_web_flasher_root': str(ROOT),
        'custom_web_flasher_source_sha256': digest,
    }
    if profile.get('recipe') == 'esp32_spp':
        defaults = (ROOT / 'tools/profiles/sdkconfig_esp32_spp.defaults').read_text()
        config['env:web_' + name]['custom_sdkconfig'] = defaults
        config['env:web_' + name]['extra_scripts'] = '${env:' + base + '.extra_scripts}\npost:' + str(ROOT / 'tools/platformio_web_flasher.py')
    with output.open('w') as stream:
        config.write(stream)
    # Never leave a previous successful binary available after a failed rebuild.
    shutil.rmtree(ARTIFACTS / name, ignore_errors=True)
    subprocess.run([pio, 'run', '-c', str(output), '-e', 'web_' + name, '-j', str(jobs)],
                   cwd=project, env=environment, check=True)
    if not (ARTIFACTS / name / 'build.json').is_file():
        raise ValueError('Build did not export firmware: ' + name)


def validate_build(profile, directory, expected_identity, expected_digest):
    report = json.loads((directory / 'build.json').read_text())
    if report.get('schema') != 1 or set(report.get('capabilities', {})) != set(CAPABILITIES):
        raise ValueError('Incomplete build metadata')
    if report['profile'] != profile['id'] or report['chip_family'] != profile['chip_family']:
        raise ValueError('Wrong firmware profile: ' + profile['id'])
    if report['identity'] != expected_identity or report['source_sha256'] != expected_digest:
        raise ValueError('Stale or mixed firmware revision: ' + profile['id'])
    if report['flash_bytes'] != profile['flash_bytes']:
        raise ValueError('Wrong flash size: ' + profile['id'])
    for service, flags in SERVICES.items():
        if any(bool(report['capabilities'][flag]) != profile['services'][service] for flag in flags):
            raise ValueError('Compiled service selection mismatch: ' + service)
    files = report['files']
    names = [item['path'] for item in files]
    if len(set(names)) != len(names) or not {'bootloader.bin', 'partitions.bin', 'firmware.bin'} <= set(names):
        raise ValueError('Incomplete or duplicate flash images')
    for item in files:
        name = item['path']
        if Path(name).name != name or not name.endswith('.bin'):
            raise ValueError('Invalid firmware path')
        path = directory / name
        if path.stat().st_size != item['size'] or sha256(path) != item['sha256']:
            raise ValueError('Firmware checksum mismatch: ' + name)
    ordered = sorted(files, key=lambda item: item['offset'])
    end = 0
    for item in ordered:
        start = item['offset']
        # These sector-aligned NVS offsets are shared by every offered profile.
        if start % 4096 or start < end or start < 0 or start + item['size'] > report['flash_bytes']:
            raise ValueError('Overlapping or out-of-range flash images')
        if start < 0xe000 and start + item['size'] > 0x9000:
            raise ValueError('Firmware would overwrite NVS')
        end = start + item['size']
    size, limit = check(directory / 'firmware.bin', directory / 'partitions.bin')
    if (size, limit) != (report['app_bytes'], report['app_limit_bytes']):
        raise ValueError('Application size metadata mismatch')
    if not any(item['path'] == 'firmware.bin' and item['offset'] == 0x10000 for item in files):
        raise ValueError('Missing application image')
    if not any(item['path'] == 'bootloader.bin' for item in files):
        raise ValueError('Missing bootloader')
    return report


def assemble(output, allow_missing=False):
    output = Path(output).resolve()
    # Only this generated directory is owned by this command.
    temporary_root = Path(tempfile.gettempdir()).resolve()
    allowed = output.is_relative_to(ROOT / 'dist') or (output != temporary_root and output.is_relative_to(temporary_root))
    if not allowed or output == ROOT or ROOT.is_relative_to(output):
        raise ValueError('Unsafe output directory')
    identity, digest = resolve(ROOT), source_digest()
    catalog = dict(schema=1, identity=identity, profiles=[])
    prepared = []
    for profile in profiles():
        directory = ARTIFACTS / profile['id']
        if not (directory / 'build.json').exists():
            if not allow_missing:
                raise ValueError('Missing firmware: ' + profile['id'])
            catalog['profiles'].append(dict(profile, build=None))
            continue
        report = validate_build(profile, directory, identity, digest)
        base = f'firmware/{identity["commit"]}-{digest[:16]}/{profile["id"]}'
        catalog['profiles'].append(dict(profile, build=report, artifact_base=base))
        prepared.append((profile, directory, report))
    shutil.rmtree(output, ignore_errors=True)
    output.mkdir(parents=True)
    shutil.copytree(ROOT / 'web-flasher/images', output / 'images')
    catalog['photos'] = json.loads((ROOT / 'web-flasher/photos.json').read_text())
    for name in ('index.html', 'app.js', 'style.css'):
        shutil.copyfile(ROOT / 'web-flasher' / name, output / name)
    (output / '.nojekyll').touch()
    (output / 'catalog.json').write_text(json.dumps(catalog, ensure_ascii=False, indent=2) + '\n')
    for profile, directory, report in prepared:
        target = output / 'firmware' / (identity['commit'] + '-' + digest[:16]) / profile['id']
        target.mkdir(parents=True)
        for item in report['files']:
            shutil.copyfile(directory / item['path'], target / item['path'])
        manifest = dict(name='G5500 · ' + profile['board_label'] + ' · ' + profile['title'],
                        version=identity['version'], new_install_prompt_erase=True,
                        new_install_improv_wait_time=0,
                        builds=[dict(chipFamily=profile['chip_family'],
                                     parts=[dict(path=item['path'], offset=item['offset'])
                                            for item in report['files']])])
        (target / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
        shutil.copyfile(directory / 'build.json', target / 'build.json')
        (target / 'SHA256SUMS').write_text(''.join(item['sha256'] + '  ' + item['path'] + '\n'
                                                    for item in report['files']))
        with zipfile.ZipFile(target / 'usb-firmware.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(target.iterdir()):
                if path.suffix != '.zip':
                    archive.write(path, path.name)
    return catalog


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    commands.add_parser('matrix')
    builder = commands.add_parser('build')
    builder.add_argument('profile', choices=[p['id'] for p in profiles()])
    builder.add_argument('--pio', default=os.environ.get('PIO', 'pio'))
    builder.add_argument('--jobs', type=int, default=4)
    site = commands.add_parser('assemble')
    site.add_argument('--output', default=str(ROOT / 'dist/web-flasher'))
    site.add_argument('--allow-missing', action='store_true')
    args = parser.parse_args()
    if args.command == 'matrix':
        print(json.dumps(dict(include=[dict(profile=p['id']) for p in profiles()])))
    elif args.command == 'build':
        build(next(p for p in profiles() if p['id'] == args.profile), args.pio, args.jobs)
    else:
        catalog = assemble(args.output, args.allow_missing)
        print(f'Prepared {sum(bool(p["build"]) for p in catalog["profiles"])}/{len(catalog["profiles"])} firmware profiles')


if __name__ == '__main__':
    main()
