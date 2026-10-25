#!/usr/bin/env python3
"""Prepare Arduino IDE/CLI copy from canonical sources with target partitions."""
import argparse
import pathlib
import shutil
import subprocess
import sys
ROOT = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--target', choices=['s3', 's2', 's2-mini', 'esp32', 'esp32-8mb', 'esp32-c3', 'crowpanel', 'wt32-eth01', 'waveshare-s3-eth'], required=True)
p.add_argument('--output', type=pathlib.Path)
a = p.parse_args()
out = a.output or ROOT / 'build' / ('arduino-' + a.target) / 'G5500_Fork'
source = ROOT / 'G5500_Fork'
subprocess.run([sys.executable, str(ROOT / 'tools/generate_web_assets.py'), '--check'], check=True)
if out.name != source.name:
    p.error('Arduino sketch directory must be named G5500_Fork')
if out.resolve() == source.resolve():
    p.error('output must differ from canonical source directory')
out.mkdir(parents=True, exist_ok=True)
# PlatformIO's generated file must never become a second copy of setup/loop.
(out / 'G5500_Fork.ino.cpp').unlink(missing_ok=True)
for f in source.iterdir():
    if f.suffix in {'.ino', '.cpp', '.c', '.hpp', '.h'} and not f.name.endswith('.ino.cpp'):
        shutil.copy2(f, out / f.name)
shutil.copytree(source / 'src', out / 'src', dirs_exist_ok=True)
partitions = 'partitions_esp32_8mb.csv' if a.target == 'esp32-8mb' else 'partitions_esp32.csv' if a.target in ('esp32','wt32-eth01','esp32-c3') else 'partitions_s3.csv' if a.target in ('s3', 'crowpanel', 'waveshare-s3-eth') else 'partitions_s2.csv'
shutil.copy2(source / partitions, out / 'partitions.csv')
print(out / 'G5500_Fork.ino')
