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


def zip_directory(directory, target):
    with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(directory.rglob('*')):
            if path.is_symlink():
                raise ValueError('Symlink in release: ' + str(path))
            if path.is_file():
                archive.write(path, path.relative_to(directory).as_posix())


def package(tag, site=ROOT / 'dist/web-flasher', artifacts=ROOT / 'artifacts/web-flasher',
            output=ROOT / 'dist/release'):
    release = check_tag(tag)
    site, artifacts, output = Path(site), Path(artifacts), Path(output)
    audit = verify(site)
    catalog = json.loads((site / 'catalog.json').read_text())
    elfs = {profile['id']: matching_elf(profile, artifacts) for profile in catalog['profiles']}
    output.mkdir(parents=True, exist_ok=True)
    zip_directory(site, output / 'web-flasher-site.zip')
    with zipfile.ZipFile(output / 'firmware-with-elf.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
        for profile in catalog['profiles']:
            directory = site / profile['artifact_base']
            for path in sorted(directory.iterdir()):
                if path.is_file() and not path.is_symlink():
                    archive.write(path, profile['id'] + '/' + path.name)
            archive.write(elfs[profile['id']], profile['id'] + '/firmware.elf')
    shutil.copyfile(site / 'source-code.zip', output / 'source-code.zip')
    release.update(source_sha256=audit['source_sha256'], profiles=[
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
        '- firmware-with-elf.zip: BIN, загрузчики, USB ZIP, контрольные суммы '
        'и точный ELF для каждого варианта.\n'
        '- web-flasher-site.zip: полный сайт; source-code.zip: исходники '
        'с сохранённой идентичностью сборки.\n\n'
        'Если после прошивки возникают проблемы, повторите установку с полным '
        'стиранием памяти. Это удалит сохранённые настройки.\n\n'
        f'Исходники: `{release["commit"]}`. Все образы собраны из этого тега.\n'
    )
    (output / 'RELEASE_NOTES.md').write_text(notes)
    names = ['firmware-with-elf.zip', 'web-flasher-site.zip', 'source-code.zip',
             'release-manifest.json', 'RELEASE_NOTES.md']
    (output / 'SHA256SUMS').write_text(''.join(sha256(output / name) + '  ' + name + '\n' for name in names))
    print(f'Release {tag}: {len(elfs)} verified firmware/ELF pairs, archives and SHA256SUMS')


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
