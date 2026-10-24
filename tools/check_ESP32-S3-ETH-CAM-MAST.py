#!/usr/bin/env python3
"""Check the exported KiCad XML netlist against the installation wiring."""
import argparse
from pathlib import Path
import re
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('netlist', type=Path, help='KiCad kicadxml netlist export')
    args = parser.parse_args()
    root = ET.parse(args.netlist).getroot()
    actual = [frozenset((n.attrib['ref'], n.attrib['pin']) for n in net.findall('node'))
              for net in root.findall('./nets/net')]
    expected = []

    def net(*pins):
        expected.append(frozenset(tuple(p.split(':', 1)) for p in pins))

    for i in range(1, 25):
        net(f'A1:FPC{i}', f'A2:{i}')
    for i in range(1, 9):
        net(f'A1:LAN{i}', f'J2:{i}')
    for driver, network, start, gpios, motor in [('A4', 'RN1', 1, [17, 16, 6], 'M1'),
                                               ('A5', 'RN2', 4, [44, 43, 7], 'M2')]:
        for i, gpio in enumerate(gpios):
            r = f'R{start+i}'
            net(f'A1:{gpio}', f'{r}:1')
            pins = [f'{r}:2', f'{driver}:{i+1}', f'{network}:{i+2}']
            if i == 2:
                pins.append(f'{driver}:4')
            net(*pins)
        net(f'{driver}:M+', f'{motor}:1')
        net(f'{driver}:M-', f'{motor}:2')
        net(f'{driver}:5')  # Current-sense outputs explicitly left unused.
        net(f'{driver}:6')
    net('PS1:1', 'F1:1', 'F2:1', 'F3:1')
    net('F1:2', 'A4:B+')
    net('F2:2', 'A5:B+')
    net('F3:2', 'A3:IN+')
    net('A3:OUT+', 'A1:5V')
    net('A1:GND', 'A3:IN-', 'A3:OUT-', 'PS1:2', 'A4:8', 'A4:B-', 'A5:8', 'A5:B-',
        'A6:GND', 'C1:2', 'C2:2', 'R15:2', 'RN1:1', 'RN2:1', 'U1:GND', 'U2:GND')
    net('A1:3V3', 'A4:7', 'A5:7', 'A6:VCC', 'C1:1', 'C2:1', 'R13:1', 'R14:1', 'U1:VDD', 'U2:VDD')
    net('A1:4', 'R13:2', 'U1:SDA', 'U2:SDA')
    net('A1:5', 'R14:2', 'U1:SCL', 'U2:SCL')
    for i, (gpio, pin) in enumerate([(19, 'DI'), (20, 'RO'), (21, 'DE')], 1):
        net(f'A1:{gpio}', f'JP{i}:1')
        pins = [f'JP{i}:2', f'A6:{pin}']
        if pin == 'DE':
            pins += ['A6:nRE', 'R15:1']
        net(*pins)
    net('A6:A', 'J4:A', 'R16:1', 'R17:1')
    net('A6:B', 'J4:B', 'R16:2', 'R17:2')
    net('A6:COM', 'J4:COM')
    missing, extra = set(expected) - set(actual), set(actual) - set(expected)
    assert not missing and not extra and len(actual) == len(expected), (
        f'Missing nets: {sorted(map(sorted, missing))}\nUnexpected nets: {sorted(map(sorted, extra))}')
    assert len(root.findall('./components/comp')) == 34
    schematic = Path(__file__).resolve().parents[1] / 'hardware/ESP32-S3-ETH-CAM-MAST/ESP32-S3-ETH-CAM-MAST.kicad_sch'
    source = schematic.read_text()
    assert not re.search(r'\((?:label|global_label|hierarchical_label|bus|sheet)\s', source), 'Hidden net shortcuts'
    assert source.count('(no_connect ') == 4
    print(f'PASS: {len(actual)} complete nets, 34 modules/components; no label or bus shortcuts')


if __name__ == '__main__':
    main()
