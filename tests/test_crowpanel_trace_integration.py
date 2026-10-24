"""Feed firmware-produced USB bytes through the recorder and playback protocol."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from crowpanel_trace_core import load_recording
from crowpanel_trace_viewer import view_command


class CrowPanelTraceIntegrationTests(unittest.TestCase):
    def test_firmware_wire_to_recording_to_renderer(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            binary = work / 'firmware-wire'
            subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++17', '-O2',
                            '-I', str(ROOT / 'G5500_Fork'),
                            str(ROOT / 'tests/crowpanel_trace_tests.cpp'),
                            '-o', str(binary)], check=True, capture_output=True)
            wire = subprocess.check_output([str(binary), '--sample'])
            incoming = work / 'usb.txt'
            incoming.write_bytes(b'boot diagnostics\n' + wire)
            journal = work / 'recording.jsonl'
            result = subprocess.run([sys.executable, str(ROOT / 'tools/crowpanel_trace.py'),
                                     'record', '--input', str(incoming), '--output', str(journal)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            recording = load_recording(journal)
            self.assertEqual(len(recording.records), 4)
            self.assertEqual(recording.duration_ms, 3)
            self.assertEqual(recording.stats.get('invalid_records', 0), 0)
            self.assertEqual(recording.stats.get('sequence_gaps', 0), 0)
            view = recording.views[0]
            self.assertEqual(view.record['view']['sourceId'], (1 << 64) - 1)
            command = view_command(int(view.time_ms), view.record).decode('ascii')
            self.assertIn('sourceId=18446744073709551615', command)
            self.assertIn('hudName=' + 'Пульт "А"\\Б'.encode().hex(), command)
            self.assertIn('speedAvailable[0]=1', command)
            self.assertIn('speedAvailable[1]=0', command)
            envelopes = [json.loads(line) for line in journal.read_text().splitlines()]
            self.assertTrue(all(item['schema'] == 'crowpanel.recording' for item in envelopes))
            replay = subprocess.run([sys.executable, str(ROOT / 'tools/crowpanel_trace.py'),
                                     'replay', str(journal), '--speed', '1000'],
                                    capture_output=True, text=True)
            self.assertEqual(replay.returncode, 0, replay.stderr)
            self.assertEqual([json.loads(line) for line in replay.stdout.splitlines()],
                             [event.record for event in recording.records])


if __name__ == '__main__':
    unittest.main()
