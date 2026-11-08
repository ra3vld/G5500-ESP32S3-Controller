#!/usr/bin/env python3
"""Package a clean tagged catalog and its matching ELF files for GitHub Releases."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import zipfile

from check_web_flasher_catalog import verify
from project_version import resolve
from web_flasher import ROOT, sha256


def check_tag(tag, root=ROOT):
    if not re.fullmatch(r'v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)', tag):
        raise ValueError('Release tag must be vMAJOR.MINOR.PATCH')
    def git(*args):
        return subprocess.check_output(['git', *args], cwd=root, text=True).strip()
    commit = git('rev-parse', 'HEAD')
    if git('rev-parse', tag + '^{commit}') != commit:
        raise ValueError('Release tag does not point to HEAD')
    identity = resolve(root)
    if identity['dirty'] or identity['version'] != tag[1:]:
        raise ValueError('Release requires a clean checkout with the exact tagged version')
    return dict(tag=tag, commit=commit, identity=identity)


def matching_elf(profile, artifacts):
    elf = artifacts / profile['id'] / 'firmware.elf'
    if not elf.is_file() or elf.is_symlink() or sha256(elf) != profile['build']['elf_sha256']:
        raise ValueError('Missing or mismatched ELF: ' + profile['id'])
    return elf


def zip_directory(directory, target, exclude=()):
    with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(directory.rglob('*')):
            if path.relative_to(directory).parts[0] in exclude:
                continue
            if path.is_symlink():
                raise ValueError('Symlink in release: ' + str(path))
            if path.is_file():
                archive.write(path, path.relative_to(directory).as_posix())


def write_archives(site, artifacts, output, catalog):
    """Keep the interface, each USB firmware and diagnostic ELF files separate."""
    site, artifacts, output = Path(site), Path(artifacts), Path(output)
    if output.exists() and any(output.iterdir()):
        raise ValueError('Release output directory must be empty')
    elfs = {p['id']: matching_elf(p, artifacts) for p in catalog['profiles']}
    output.mkdir(parents=True, exist_ok=True)
    archives = dict(website='web-flasher-site.zip', source='source-code.zip',
                    elf='diagnostic-elf.zip', firmware={})
    zip_directory(site, output / archives['website'], exclude={'firmware', 'documentation', 'source-code.zip'})
    for profile in catalog['profiles']:
        name = 'firmware-' + profile['id'] + '.zip'
        shutil.copyfile(site / profile['artifact_base'] / 'usb-firmware.zip', output / name)
        archives['firmware'][profile['id']] = name
    with zipfile.ZipFile(output / archives['elf'], 'w', zipfile.ZIP_DEFLATED) as archive:
        for ident, elf in elfs.items():
            archive.write(elf, ident + '/firmware.elf')
    shutil.copyfile(site / 'source-code.zip', output / archives['source'])
    return archives


def package(tag, site=ROOT / 'dist/web-flasher', artifacts=ROOT / 'artifacts/web-flasher',
            output=ROOT / 'dist/release'):
    release = check_tag(tag)
    site, artifacts, output = Path(site), Path(artifacts), Path(output)
    audit = verify(site)
    catalog = json.loads((site / 'catalog.json').read_text())
    archives = write_archives(site, artifacts, output, catalog)
    release.update(schema=2, archives=archives, source_sha256=audit['source_sha256'], profiles=[
        dict(id=p['id'], chip_family=p['chip_family'], services=p['services'],
             app_bytes=p['build']['app_bytes'], app_limit_bytes=p['build']['app_limit_bytes'],
             elf_sha256=p['build']['elf_sha256'], files=p['build']['files'])
        for p in catalog['profiles']])
    (output / 'release-manifest.json').write_text(json.dumps(release, ensure_ascii=False, indent=2) + '\n')
    notes = (
        f'G5500 Controller {tag}: {len(catalog["profiles"])} готовых вариантов прошивки.\n\n'
        '[Открыть веб-конфигуратор и прошивальщик]'
        '(https://ra3vld.github.io/G5500-ESP32S3-Controller/).\n\n'
        '- Веб-конфигуратор: выбор платы, дисплея и доступного набора функций, '
        'фотографии плат, размеры прошивки и прошивка из браузера.\n'
        '- После установки доступны инструкции и схема подключений выбранной платы.\n'
        '- ESP32: полная версия без SPP и компактная версия с SPP.\n'
        '- firmware-<профиль>.zip: отдельный USB-комплект выбранной прошивки, '
        'загрузчик, разметка, manifest.json и SHA256SUMS.\n'
        '- web-flasher-site.zip: интерфейс без BIN/ELF и встроенной документации; '
        'документация открывается на GitHub.\n'
        '- diagnostic-elf.zip: точные ELF всех вариантов для диагностики.\n'
        '- source-code.zip: исходники с сохранённой идентичностью сборки.\n\n'
        'Если после прошивки возникают проблемы, повторите установку с полным '
        'стиранием памяти. Это удалит сохранённые настройки.\n\n'
        f'Исходники: `{release["commit"]}`. Все образы собраны из этого тега.\n'
    )
    notes += '\n| Плата | Конфигурация | USB-комплект |\n|---|---|---|\n'
    for profile in catalog['profiles']:
        name = archives['firmware'][profile['id']]
        services = 'MQTT + RS-485' if profile['services']['mqtt'] and profile['services']['rs485'] else 'без MQTT / RS-485' if not profile['services']['mqtt'] and not profile['services']['rs485'] else ', '.join(k.upper() for k, v in profile['services'].items() if v)
        notes += f'| {profile["board_label"]} | {profile["title"]} · {services} | [{name}](https://github.com/ra3vld/G5500-ESP32S3-Controller/releases/download/{tag}/{name}) |\n'
    (output / 'RELEASE_NOTES.md').write_text(notes)
    names = [archives['website'], archives['source'], archives['elf'],
             *archives['firmware'].values(), 'release-manifest.json', 'RELEASE_NOTES.md']
    (output / 'SHA256SUMS').write_text(''.join(sha256(output / name) + '  ' + name + '\n' for name in names))
    print(f'Release {tag}: {len(archives["firmware"])} separate USB archives, interface, matching ELF and SHA256SUMS')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--check-tag')
    mode.add_argument('--tag')
    args = parser.parse_args()
    if args.check_tag:
        print(json.dumps(check_tag(args.check_tag)))
    else:
        package(args.tag)
