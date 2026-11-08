#!/usr/bin/env python3
"""Verify every catalog image, manifest, archive and optionally the live server."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import urllib.request
from urllib.parse import urljoin, quote
import zipfile
from web_flasher import profiles, source_digest, validate_build, sha256, planned_capabilities
from project_version import RELEASE, render, resolve

CHIP_IDS = {'ESP32': 0, 'ESP32-S2': 2, 'ESP32-C3': 5, 'ESP32-S3': 9}


def released_identity(tag, root=Path(__file__).resolve().parents[1]):
    if not tag.startswith('v') or not RELEASE.fullmatch(tag[1:]):
        raise ValueError('Expected a stable vMAJOR.MINOR.PATCH release tag')
    commit = subprocess.check_output(
        ['git', 'rev-parse', '--verify', '--short=7', 'refs/tags/' + tag + '^{commit}'],
        cwd=root, text=True).strip()
    return dict(version=tag[1:], commit=commit, dirty=False)


def verify(site, base_url=None, firmware_tag=None):
    site = Path(site).resolve()
    catalog = json.loads((site / 'catalog.json').read_text())
    expected = profiles()
    assert len(catalog['profiles']) == len(expected)
    assert [p['id'] for p in catalog['profiles']] == [p['id'] for p in expected]
    identity = released_identity(firmware_tag) if firmware_tag else resolve()
    digest = source_digest()
    assert catalog['identity'] == identity
    files = {'catalog.json', 'index.html', 'app.js', 'style.css', 'provisioning.js', 'serial-api.js', 'THIRD_PARTY_LICENSES.txt', 'images/favicon.svg'}
    for name in ('provisioning.js','serial-api.js'):
        assert (site / name).read_bytes() == (Path(__file__).resolve().parents[1] / 'web-flasher' / name).read_bytes()
    assert (site / 'images/favicon.svg').read_bytes() == (Path(__file__).resolve().parents[1] / 'web-flasher/images/favicon.svg').read_bytes()
    revisions = set()
    for profile, actual in zip(expected, catalog['profiles']):
        assert actual['build'] is not None, profile['id']
        for key in ('base_id', 'board', 'chip_family', 'display_kind', 'services', 'available_services', 'selectable_services', 'required_capabilities', 'flash_bytes', 'psram_bytes', 'docs', 'photo'):
            assert actual.get(key) == profile.get(key), (profile['id'], key)
        relative = actual['artifact_base']
        assert relative == 'firmware/' + identity['commit'] + '-' + digest[:16] + '/' + profile['id']
        directory = site / relative
        report = validate_build(profile, directory, identity, digest)
        assert actual['build'] == report
        assert actual['planned_capabilities'] == planned_capabilities(profile) == report['capabilities'], profile['id']
        revisions.add(str(Path(relative).parent))
        raw = (directory / 'firmware.bin').read_bytes()
        assert raw[0] == 0xe9 and struct.unpack_from('<H', raw, 12)[0] == CHIP_IDS[profile['chip_family']], profile['id']
        boot = (directory / 'bootloader.bin').read_bytes()
        assert boot[0] == 0xe9
        assert boot[3] >> 4 == profile['flash_bytes'].bit_length() - 21, profile['id']
        manifest = json.loads((directory / 'manifest.json').read_text())
        assert manifest['version'] == identity['version']
        assert manifest['new_install_prompt_erase'] is True
        assert manifest['builds'] == [dict(chipFamily=profile['chip_family'], parts=[dict(path=f['path'],offset=f['offset']) for f in report['files']])]
        sums = ''.join(f['sha256'] + '  ' + f['path'] + '\n' for f in report['files'])
        assert (directory / 'SHA256SUMS').read_text() == sums
        with zipfile.ZipFile(directory / 'usb-firmware.zip') as archive:
            assert archive.testzip() is None
            assert len(archive.namelist()) == len(set(archive.namelist()))
            for name in ['manifest.json', 'build.json', 'SHA256SUMS', *[f['path'] for f in report['files']]]:
                assert archive.read(name) == (directory / name).read_bytes(), (profile['id'], name)
        for name in ['manifest.json', 'build.json', 'SHA256SUMS', 'usb-firmware.zip', *[f['path'] for f in report['files']]]:
            files.add(relative + '/' + name)
        doc = 'documentation/' + str(Path(profile['docs']).with_suffix('.html'))
        assert (site / doc).is_file(); files.add(doc)
    assert len(revisions) == 1
    actual_folders = {str(p.relative_to(site)) for p in (site / 'firmware').iterdir() if p.is_dir()}
    assert actual_folders == revisions
    assert {p.name for p in (site / next(iter(revisions))).iterdir()} == {p['id'] for p in expected}
    for photo in catalog['photos'].values():
        relative = 'images/' + photo['file']
        assert sha256(site / relative) == photo['sha256']; files.add(relative)
    docs = json.loads((site / 'documentation-check.json').read_text())
    assert docs['broken_links'] == []
    files.update(str(p.relative_to(site)) for p in (site / 'documentation').rglob('*') if p.is_file())
    files.update(str(p.relative_to(site)) for p in (site / 'vendor').rglob('*') if p.is_file())
    assert sha256(site / 'source-code.zip') == catalog['source_archive_sha256']
    files.add('source-code.zip')
    with zipfile.ZipFile(site / 'source-code.zip') as archive:
        assert archive.testzip() is None
        assert 'G5500_Fork/build_identity.hpp' in archive.namelist()
        assert archive.read('G5500_Fork/build_identity.hpp').decode() == render(identity)
        for name in ['requirements-platformio.txt', 'platformio/esp32_spp.ini', '.github/workflows/web-flasher.yml']:
            root = Path(__file__).resolve().parents[1]
            expected_source = subprocess.check_output(['git', 'show', firmware_tag + ':' + name], cwd=root) if firmware_tag else (root / name).read_bytes()
            assert archive.read(name) == expected_source
        assert all('local-notes' not in Path(name).parts and '.pio' not in Path(name).parts for name in archive.namelist())
    print('PASS static catalog: ' + str(len(expected)) + ' images, one source revision, actual chip/flash/display/services, ELF identity, all manifests and USB archives', flush=True)
    if base_url:
        base_url = base_url.rstrip('/') + '/'
        def remote(relative):
            request = urllib.request.Request(urljoin(base_url, quote(relative)), headers={'Cache-Control':'no-cache'})
            with urllib.request.urlopen(request, timeout=30) as response:
                data = response.read()
            assert hashlib.sha256(data).hexdigest() == sha256(site / relative), relative
        with ThreadPoolExecutor(max_workers=4) as pool:
            list(pool.map(remote, sorted(files)))
        print('PASS live server: ' + str(len(files)) + ' files match the checked catalog byte for byte', flush=True)
    return dict(profiles=len(expected), source_sha256=digest, identity=identity, checked_files=len(files), live_server=base_url)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--site', default='dist/web-flasher')
    parser.add_argument('--url')
    parser.add_argument('--report')
    parser.add_argument('--firmware-tag', help='Verify unchanged firmware from this release with the current website')
    args = parser.parse_args()
    result = verify(args.site, args.url, args.firmware_tag)
    if args.report:
        Path(args.report).write_text(json.dumps(result, indent=2) + '\n')
