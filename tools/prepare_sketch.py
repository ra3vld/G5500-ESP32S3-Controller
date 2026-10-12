#!/usr/bin/env python3
"""Prepare Arduino IDE/CLI copy from canonical sources with target partitions."""
import argparse
import pathlib
import shutil
ROOT = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--target', choices=['s3', 's2'], required=True)
p.add_argument('--output', type=pathlib.Path)
a = p.parse_args()
out = a.output or ROOT / 'build' / ('arduino-' + a.target) / 'G5500_Fork'
source = ROOT / 'G5500_Fork'
if out.name != source.name:
    p.error('Arduino sketch directory must be named G5500_Fork')
if out.resolve() == source.resolve():
    p.error('output must differ from canonical source directory')
out.mkdir(parents=True, exist_ok=True)
# PlatformIO's generated file must never become a second copy of setup/loop.
(out / 'G5500_Fork.ino.cpp').unlink(missing_ok=True)
for f in source.iterdir():
    if f.suffix in {'.ino', '.cpp', '.hpp', '.h'} and not f.name.endswith('.ino.cpp'):
        shutil.copy2(f, out / f.name)
shutil.copy2(source / ('partitions_s2.csv' if a.target == 's2' else 'partitions.csv'), out / 'partitions.csv')
print(out / 'G5500_Fork.ino')
