"""Portable firmware trace serialization and bounded USB queue contract."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CrowPanelUsbTraceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.binary = Path(cls.temporary.name) / 'crowpanel_trace_tests'
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-I', str(ROOT / 'G5500_Fork'), str(ROOT / 'tests/crowpanel_trace_tests.cpp'),
                        '-o', str(cls.binary)], check=True)

    def test_queue_backpressure(self):
        subprocess.run([str(self.binary)], check=True, capture_output=True, text=True)

    def test_wire_schema_and_every_view_field(self):
        wire = subprocess.check_output([str(self.binary), '--sample'], text=True)
        rows = [json.loads(line.removeprefix('@CPTRACE ')) for line in wire.splitlines() if line]
        self.assertEqual([row['type'] for row in rows], ['meta', 'view', 'input', 'input'])
        for seq, row in enumerate(rows, 1):
            self.assertEqual(row['schema'], 'crowpanel.trace')
            self.assertEqual(row['v'], 1)
            self.assertEqual(row['seq'], seq)
            self.assertEqual(row['boot'], '000000000000002a')
            self.assertEqual(row['dropped'], 0)
            self.assertIs(type(row['ms']), int)
        self.assertEqual((rows[0]['width'], rows[0]['height']), (480, 480))
        self.assertEqual(rows[0]['elfSha256'], 'abcdef')
        view = rows[1]['view']
        bools = '''movingNow settingsAvailable enabled hud hudSelector dualAxis speedScreen elevation feedback targetValid uncalibrated
                   secondaryUncalibrated pending moving autoMode autoHour secondaryFeedback
                   secondaryTargetValid secondaryPending wifiConnected wifiAp bluetoothEnabled
                   bluetoothConnected statusFault'''.split()
        numbers = '''direction current target minimum maximum secondaryCurrent secondaryTarget
                     azDirection elDirection sourceId wifiRssi'''.split()
        texts = 'hudName hudAddress ip heap status'.split()
        arrays = ['hudRows', 'speedAvailable', 'speedPercent']
        self.assertEqual(set(view), set(bools + numbers + texts + arrays))
        for name in bools:
            self.assertIs(type(view[name]), bool, name)
        for name in numbers:
            self.assertIn(type(view[name]), (int, float), name)
        for name in texts:
            self.assertIs(type(view[name]), str, name)
        self.assertEqual(view['hudName'], 'Пульт "А"\\Б')
        self.assertEqual(view['hudRows'][0], 'Строка\nещё\t')
        self.assertEqual(view['sourceId'], 2 ** 64 - 1)
        self.assertEqual(view['speedAvailable'], [True, False])
        self.assertEqual(view['speedPercent'], [75, 5])
        self.assertAlmostEqual(view['current'], 123.456787109375, places=6)
        self.assertEqual(rows[2]['input'], {'kind': 'knob', 'healthy': True, 'down': True, 'delta': -2})
        self.assertEqual(rows[3]['input']['kind'], 'touch')
        self.assertEqual((rows[3]['input']['x'], rows[3]['input']['y']), (120, 70))
        self.assertEqual((rows[3]['input']['axis'], rows[3]['input']['percent']), (1, 33))
        self.assertIs(type(rows[3]['input']['absoluteSpeed']), bool)
        self.assertEqual(rows[1]['state']['axes'][0]['current'], 123.457)


if __name__ == '__main__':
    unittest.main()
