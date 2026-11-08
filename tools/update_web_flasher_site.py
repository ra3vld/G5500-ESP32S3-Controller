#!/usr/bin/env python3
"""Update installer assets while preserving a verified release's firmware and sources."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile

from check_web_flasher_catalog import verify
from project_version import resolve
from web_flasher import ROOT, sha256

ASSETS = ('index.html', 'app.js', 'style.css', 'provisioning.js', 'serial-api.js', 'images/favicon.svg')


def verify_archive(archive, checksums):
    entries = [line.split('  ', 1) for line in Path(checksums).read_text().splitlines()]
    expected = [digest for digest, name in entries if name == Path(archive).name]
    if len(expected) != 1 or not Path(archive).is_file() or Path(archive).is_symlink() or sha256(archive) != expected[0]:
        raise ValueError('Release archive checksum mismatch: ' + Path(archive).name)


def extract_release(archive, output):
    output = Path(output)
    if output.exists():
        raise ValueError('Output directory must not exist')
    with zipfile.ZipFile(archive) as package:
        names = package.namelist()
        if len(names) != len(set(names)):
            raise ValueError('Duplicate release archive paths')
        for entry in package.infolist():
            path = Path(entry.filename)
            if path.is_absolute() or '..' in path.parts or '\\' in entry.filename or (entry.external_attr >> 16) & 0o170000 == 0o120000:
                raise ValueError('Unsafe release archive path')
        if package.testzip() is not None:
            raise ValueError('Corrupt release archive')
        package.extractall(output)


def restore_firmware(output, release_files, checksums, tag):
    """Assemble Pages from separate verified archives; accept old full-site releases."""
    output, release_files = Path(output), Path(release_files)
    manifest_file = release_files / 'release-manifest.json'
    if manifest_file.is_file():
        verify_archive(manifest_file, checksums)
        manifest = json.loads(manifest_file.read_text())
    else:
        manifest = {}
    if manifest.get('schema') != 2:
        if not (output / 'firmware').is_dir():
            raise ValueError('Separate firmware archives require release-manifest.json schema 2')
        return {}
    if (output / 'firmware').exists() or (output / 'documentation').exists() or (output / 'source-code.zip').exists():
        raise ValueError('The separate website archive must not embed firmware, sources or documentation')
    catalog = json.loads((output / 'catalog.json').read_text())
    archives = manifest['archives']
    if (manifest['tag'] != tag or manifest['identity'] != catalog['identity'] or
            archives['website'] != 'web-flasher-site.zip' or archives['source'] != 'source-code.zip'):
        raise ValueError('Release archive identity mismatch')
    expected = {p['id']: 'firmware-' + p['id'] + '.zip' for p in catalog['profiles']}
    if archives['firmware'] != expected:
        raise ValueError('Release firmware archive set differs from the catalog')
    digest = manifest['source_sha256']
    if not re.fullmatch(r'[0-9a-f]{64}', digest):
        raise ValueError('Invalid release source digest')
    checked = {}
    for profile in catalog['profiles']:
        ident = profile['id']
        if not re.fullmatch(r'[a-z0-9_]+', ident):
            raise ValueError('Unsafe firmware profile ID')
        relative = 'firmware/' + catalog['identity']['commit'] + '-' + digest[:16] + '/' + ident
        if profile['artifact_base'] != relative:
            raise ValueError('Unsafe firmware artifact path')
        archive = release_files / expected[ident]
        verify_archive(archive, checksums)
        names = {'manifest.json', 'build.json', 'SHA256SUMS', *[f['path'] for f in profile['build']['files']]}
        with zipfile.ZipFile(archive) as package:
            if set(package.namelist()) != names:
                raise ValueError('USB archive contents differ from the catalog: ' + ident)
        folder = output / relative
        folder.parent.mkdir(parents=True, exist_ok=True)
        extract_release(archive, folder)
        shutil.copyfile(archive, folder / 'usb-firmware.zip')
        checked[archive.name] = sha256(archive)
    source = release_files / archives['source']
    verify_archive(source, checksums)
    if sha256(source) != catalog['source_archive_sha256']:
        raise ValueError('Released source archive differs from the catalog')
    shutil.copyfile(source, output / 'source-code.zip')
    checked[source.name] = sha256(source)
    return checked


def update(archive, checksums, tag, output=ROOT / 'dist/web-flasher', allow_dirty=False):
    ui = resolve()
    if ui['dirty'] and not allow_dirty:
        raise ValueError('Commit website sources before publishing; --allow-dirty is for local previews')
    verify_archive(archive, checksums)
    extract_release(archive, output)
    output = Path(output)
    restored = restore_firmware(output, Path(archive).parent, checksums, tag)
    original_catalog = (output / 'catalog.json').read_bytes()
    firmware = {str(p.relative_to(output)): sha256(p) for p in (output / 'firmware').rglob('*') if p.is_file()}
    for name in ASSETS:
        target = output / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'web-flasher' / name, target)
    vendor = subprocess.check_output(['node', str(ROOT / 'tools/patch_web_flasher_vendor.mjs'), str(output)], text=True).strip()
    subprocess.run([sys.executable, str(ROOT / 'tools/build_web_flasher_docs.py'), '--output', str(output), '--website-only', '--external'], check=True)
    audit = verify(output, firmware_tag=tag)
    assert (output / 'catalog.json').read_bytes() == original_catalog
    assert firmware == {str(p.relative_to(output)): sha256(p) for p in (output / 'firmware').rglob('*') if p.is_file()}
    record = dict(schema=1, website=ui, firmware_tag=tag, firmware=audit['identity'],
                  firmware_source_sha256=audit['source_sha256'], release_archive_sha256=sha256(archive),
                  separate_archives=restored,
                  assets={name: sha256(output / name) for name in (*ASSETS, vendor, 'documentation-check.json',
                      *(str(p.relative_to(output)) for p in sorted((output / 'documentation').rglob('*')) if p.is_file()))})
    (output / 'site-identity.json').write_text(json.dumps(record, indent=2) + '\n')
    print(f'Website updated: {audit["profiles"]} released firmware profiles unchanged; UI {ui["commit"]}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--checksums', type=Path, required=True)
    parser.add_argument('--tag', required=True)
    parser.add_argument('--output', type=Path, default=ROOT / 'dist/web-flasher')
    parser.add_argument('--allow-dirty', action='store_true', help='Allow an uncommitted local preview')
    args = parser.parse_args()
    update(args.archive, args.checksums, args.tag, args.output, args.allow_dirty)
