#!/usr/bin/env python3
"""Create a source release from Git-tracked files in the current working tree."""
from pathlib import Path
import hashlib
import subprocess
import zipfile
from project_version import resolve, render


def main():
    root = Path(__file__).resolve().parents[1]
    paths = subprocess.check_output(
        ['git', 'ls-files', '-z'], cwd=root
    ).decode().split('\0')
    sources = []
    for name in sorted(filter(None, paths)):
        source = root / name
        if source.is_symlink() or not source.is_file():
            raise SystemExit(f'Expected a regular source file: {name}')
        sources.append((name, source.read_bytes(), source.stat().st_mode & 0o111))
    if not sources:
        raise SystemExit('No tracked files; add source files to Git first.')

    out = root / 'artifacts'
    sources.append(('G5500_Fork/build_identity.hpp', render(resolve(root)).encode(), 0))
    out.mkdir(exist_ok=True)
    manifest = ''.join(
        hashlib.sha256(data).hexdigest() + '  ' + name + '\n'
        for name, data, _ in sources
    ).encode()
    archive_path = out / 'g5500-controller-sources.zip'
    with zipfile.ZipFile(archive_path, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, data, executable in sources + [('SOURCE_SHA256SUMS', manifest, 0)]:
            entry = zipfile.ZipInfo('g5500-controller/' + name)
            entry.create_system = 3
            entry.external_attr = (0o100755 if executable else 0o100644) << 16
            entry.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(entry, data)

    (out / 'SOURCE_SHA256SUMS').write_bytes(manifest)
    checksum = hashlib.sha256(archive_path.read_bytes()).hexdigest()
    (out / 'SHA256SUMS').write_text(checksum + '  ' + archive_path.name + '\n')
    print(f'Packaged {len(sources)} tracked files: {archive_path}')


if __name__ == '__main__':
    main()
