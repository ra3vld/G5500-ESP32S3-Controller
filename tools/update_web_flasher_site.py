#!/usr/bin/env python3
"""Update installer assets while preserving a verified release's firmware and sources."""
import argparse
import json
from pathlib import Path
import shutil
import zipfile

from check_web_flasher_catalog import verify
from project_version import resolve
from web_flasher import ROOT, sha256

ASSETS = ('index.html', 'app.js', 'style.css', 'images/favicon.svg')


def verify_archive(archive, checksums):
    entries = [line.split('  ', 1) for line in Path(checksums).read_text().splitlines()]
    expected = [digest for digest, name in entries if name == Path(archive).name]
    if len(expected) != 1 or sha256(archive) != expected[0]:
        raise ValueError('Release website archive checksum mismatch')


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


def update(archive, checksums, tag, output=ROOT / 'dist/web-flasher', allow_dirty=False):
    ui = resolve()
    if ui['dirty'] and not allow_dirty:
        raise ValueError('Commit website sources before publishing; --allow-dirty is for local previews')
    verify_archive(archive, checksums)
    extract_release(archive, output)
    output = Path(output)
    original_catalog = (output / 'catalog.json').read_bytes()
    firmware = {str(p.relative_to(output)): sha256(p) for p in (output / 'firmware').rglob('*') if p.is_file()}
    for name in ASSETS:
        target = output / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'web-flasher' / name, target)
    audit = verify(output, firmware_tag=tag)
    assert (output / 'catalog.json').read_bytes() == original_catalog
    assert firmware == {str(p.relative_to(output)): sha256(p) for p in (output / 'firmware').rglob('*') if p.is_file()}
    record = dict(schema=1, website=ui, firmware_tag=tag, firmware=audit['identity'],
                  firmware_source_sha256=audit['source_sha256'], release_archive_sha256=sha256(archive),
                  assets={name: sha256(output / name) for name in ASSETS})
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
