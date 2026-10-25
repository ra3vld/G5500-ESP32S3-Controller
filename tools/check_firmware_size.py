#!/usr/bin/env python3
"""Check the complete ESP application image against its CSV application slots."""
import argparse
import csv
from pathlib import Path


def partition_bytes(value):
    value = value.strip().lower()
    if value.endswith(('k', 'm')):
        return int(value[:-1], 0) * (1024 if value[-1] == 'k' else 1024 * 1024)
    return int(value, 0)


def check(image, partitions):
    rows = csv.reader(line for line in Path(partitions).read_text().splitlines()
                      if line.strip() and not line.lstrip().startswith('#'))
    slots = [partition_bytes(row[4]) for row in rows
             if len(row) >= 5 and row[1].strip() in ('app', '0', '0x00')]
    if not slots:
        raise ValueError('No application slot in partition table')
    size, limit = Path(image).stat().st_size, min(slots)
    if size > limit:
        raise ValueError(f'Firmware image {size} bytes exceeds application slot {limit} bytes')
    return size, limit


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('partitions', type=Path)
    args = parser.parse_args()
    try:
        size, limit = check(args.image, args.partitions)
    except ValueError as error:
        parser.exit(1, str(error) + '\n')
    print(f'Application image: {size}/{limit} bytes; {limit-size} bytes free')
