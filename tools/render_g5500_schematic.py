#!/usr/bin/env python3
"""Export the editable G-5500 TFT SVG to PNG/PDF; requires rsvg-convert."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'docs/hardware/g5500/g5500-esp32s3-tft-schematic.svg'


def main():
    for extension in ('png', 'pdf'):
        target = SOURCE.with_suffix('.' + extension)
        command = ['rsvg-convert', '--format', extension, '--output', str(target)]
        if extension == 'png':
            command += ['--width', '3360', '--height', '2376']
        subprocess.run(command + [str(SOURCE)], check=True)
        print(target)


if __name__ == '__main__':
    main()
