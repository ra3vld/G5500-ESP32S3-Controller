"""Derive firmware defaults from the selected flash size and partition table."""
import csv
from pathlib import Path


def flash_bytes(value):
    value = str(value).strip().lower()
    for suffix, factor in (('mb', 1 << 20), ('m', 1 << 20), ('kb', 1 << 10), ('k', 1 << 10)):
        if value.endswith(suffix):
            return int(value[:-len(suffix)], 0) * factor
    return int(value, 0)


def partition_capabilities(partitions, flash):
    slots = set()
    metadata = False
    rows = csv.reader(line for line in Path(partitions).read_text().splitlines()
                      if line.strip() and not line.lstrip().startswith('#'))
    for row in rows:
        if len(row) < 5:
            raise ValueError('Incomplete partition row: ' + repr(row))
        kind, subtype = (value.strip().lower() for value in row[1:3])
        kind = {'app': 0, 'data': 1}.get(kind, kind)
        if isinstance(kind, str):
            kind = int(kind, 0)
        if kind == 0:
            if subtype.startswith('ota_'):
                subtype = 0x10 + int(subtype[4:])
            elif subtype not in ('factory', 'test'):
                subtype = int(subtype, 0)
            if isinstance(subtype, int) and 0x10 <= subtype < 0x20:
                slots.add(subtype)
        elif kind == 1:
            subtype = 0 if subtype == 'ota' else int(subtype, 0) if subtype[:1].isdigit() else None
            if subtype == 0 and flash_bytes(row[4]) >= 0x2000:
                metadata = True
    return {'G5500_BUILD_FLASH_BYTES': flash_bytes(flash),
            'G5500_HAS_OTA_PARTITIONS': int(metadata and len(slots) >= 2)}


def write_capabilities(path, values):
    text = '// Generated from the selected flash and partition table.\n#pragma once\n'
    for name, value in values.items():
        text += f'#ifndef {name}\n#define {name} {value}\n#endif\n'
    Path(path).write_text(text)
