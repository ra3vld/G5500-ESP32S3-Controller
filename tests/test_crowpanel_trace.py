"""Trace corruption, clock/session handling and read-only serial integration."""
from __future__ import annotations

import io
import json
import os
from pathlib import Path
import select
import signal
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from crowpanel_trace_core import (JOURNAL_SCHEMA, TraceDecoder, UINT32, UINT64,
                                 WIRE_PREFIX, demo_records, journal_line,
                                 load_recording, playback_records, validate_record)


def wire(seq=0, ms=0, boot="aabb", kind="view", **fields):
    value = dict(schema="crowpanel.trace", v=1, type=kind, boot=boot, seq=seq, ms=ms, dropped=0)
    if kind == "view":
        value.update(view={"status": "Движение · AZ", "current": 12.5}, pressed=0)
    elif kind == "input":
        value["input"] = {"kind": "touch", "down": True, "x": 240, "y": 100}
    value.update(fields)
    return value


def line(record):
    return WIRE_PREFIX + json.dumps(record, ensure_ascii=False).encode() + b"\n"


def journal(records):
    return io.BytesIO("".join(journal_line(value, host) for value, host in records).encode())


class DecoderTests(unittest.TestCase):
    def test_chunk_boundaries_utf8_diagnostics_and_incomplete_tail(self):
        record = wire()
        raw = b"ESP-ROM\n\xff\xfe diagnostics\r\n" + line(record) + b"@CPTRACE {bad}\n" + line(wire(1, 20))[:-4]
        decoder = TraceDecoder()
        accepted = []
        for byte in raw:
            accepted.extend(decoder.feed(bytes([byte])))
        decoder.finish()
        self.assertEqual([item.record for item in accepted], [record])
        self.assertEqual(decoder.stats["diagnostic_lines"], 2)
        self.assertEqual(decoder.stats["malformed_lines"], 1)
        self.assertEqual(decoder.stats["truncated_lines"], 1)

    def test_bounded_line_resynchronizes_after_newline(self):
        decoder = TraceDecoder(max_line_bytes=512)
        self.assertFalse(decoder.feed(WIRE_PREFIX + b"x" * 400))
        self.assertFalse(decoder.feed(b"y" * 100000))
        self.assertLessEqual(len(decoder._buffer), 512)
        accepted = decoder.feed(b"tail\n" + line(wire()))
        self.assertEqual(len(accepted), 1)
        self.assertEqual(decoder.stats["oversized_lines"], 1)
        decoder.finish()
        self.assertEqual(decoder.stats["truncated_lines"], 0)

    def test_invalid_utf8_inside_trace_is_not_replacement_decoded(self):
        decoder = TraceDecoder()
        accepted = decoder.feed(line(wire()).replace("Движение".encode(), b"\xff") + line(wire(1, 1)))
        self.assertEqual([item.record["seq"] for item in accepted], [1])
        self.assertEqual(decoder.stats["malformed_lines"], 1)

    def test_invalid_envelopes_and_nonfinite_numbers(self):
        bad = [wire(seq=True), wire(ms=-1), wire(ms=UINT32), wire(seq=UINT64),
               wire(dropped=UINT64), wire(boot="not hex"), wire(pressed=7),
               wire(v=True), wire(v=2), wire(kind="other"), wire(view=[]),
               wire(view={"current": float("nan")}), wire(view={"current": float("inf")})]
        for record in bad:
            self.assertFalse(validate_record(record), record)
        decoder = TraceDecoder()
        malformed_journal = {"schema": JOURNAL_SCHEMA, "v": 1, "host_ms": -1, "record": wire()}
        raw = b"".join(line(value) for value in bad)
        raw += json.dumps(malformed_journal).encode() + b"\n"
        self.assertFalse(decoder.feed(raw))
        self.assertEqual(decoder.stats["invalid_records"], len(bad) + 1)

    def test_bad_host_timestamps_unicode_surrogates_and_input_pressed_recover(self):
        decoder = TraceDecoder()
        envelopes = [{"schema": JOURNAL_SCHEMA, "v": 1, "host_ms": value, "record": wire()}
                     for value in (10 ** 1000, True, float("nan"), float("inf"), -1)]
        invalid = [wire(view={"status": "\ud800"}), wire(view={"\udfff": 1}),
                   wire(kind="input", pressed=999), wire(kind="input", pressed=True)]
        raw = b"".join(json.dumps(value).encode() + b"\n" for value in envelopes + invalid)
        accepted = decoder.feed(raw + line(wire(kind="input", pressed=1)))
        self.assertEqual(len(accepted), 1)
        self.assertEqual(decoder.stats["invalid_records"], len(envelopes) + len(invalid))

    def test_preserves_identity_uint64_and_unknown_fields(self):
        meta = wire(kind="meta", firmware="g5500", sourceGit="0123456", elfSha256="abcdef", other={"keep": True})
        view = wire(seq=1, ms=30, view={"sourceId": UINT64 - 1, "status": "Связь"})
        recording = load_recording(journal([(meta, 1.25), (view, 31.25)]))
        self.assertEqual([item.record for item in recording.records], [meta, view])
        self.assertEqual(recording.records[0].host_ms, 1.25)
        self.assertEqual(recording.duration_ms, 30)

    def test_empty_and_partial_recordings_are_safe(self):
        empty = load_recording(io.BytesIO(b""))
        self.assertEqual(empty.records, [])
        self.assertEqual(empty.duration_ms, 0)
        partial = load_recording(io.BytesIO(line(wire())[:-1]))
        self.assertFalse(partial.records)
        self.assertEqual(partial.stats["truncated_lines"], 1)


class TimelineTests(unittest.TestCase):
    def test_uint64_sequence_uint32_clock_wrap_reconnect_and_reboot(self):
        records = [
            (wire(seq=UINT64 - 2, ms=UINT32 - 15, kind="meta", dropped=5), 0),
            (wire(seq=0, ms=5, dropped=7), 20),
            (wire(seq=1, ms=5, kind="meta", dropped=7), 21),
            (wire(seq=2, ms=15, kind="input", dropped=7), 30),
            (wire(seq=0, ms=0, boot="bb", dropped=0), 90),
        ]
        recording = load_recording(journal(records))
        self.assertEqual([item.time_ms for item in recording.records], [0, 20, 20, 30, 90])
        self.assertEqual([item.session for item in recording.records], [0, 0, 0, 0, 1])
        self.assertEqual(recording.stats["sessions"], 2)
        self.assertEqual(recording.stats["reboots"], 1)
        self.assertEqual(recording.stats["millis_wraps"], 1)
        self.assertEqual(recording.stats["sequence_gaps"], 1)
        self.assertEqual(recording.stats["device_drops"], 2)
        self.assertEqual(recording.stats["device_drops_before_capture"], 5)

    def test_high_dropped_counter_and_rollover(self):
        recording = load_recording(journal([
            (wire(seq=1 << 45, dropped=UINT64 - 2), 0),
            (wire(seq=(1 << 45) + 1, ms=10, dropped=1), 10),
        ]))
        self.assertEqual(len(recording.records), 2)
        self.assertEqual(recording.stats["device_drops"], 3)
        self.assertEqual(recording.stats["device_drops_before_capture"], UINT64 - 2)

    def test_stale_records_do_not_rewind_or_inflate_following_interval(self):
        recording = load_recording(journal([
            (wire(seq=3, ms=100), 0), (wire(seq=2, ms=90), 1), (wire(seq=4, ms=110), 10),
        ]))
        self.assertEqual([item.time_ms for item in recording.records], [0, 0, 10])
        self.assertEqual(recording.stats["out_of_order"], 1)
        self.assertEqual(recording.stats.get("sequence_gaps", 0), 0)

    def test_clock_reset_keeps_observed_host_interval(self):
        recording = load_recording(journal([
            (wire(seq=0, ms=100, dropped=5), 10), (wire(seq=1, ms=10, dropped=7), 60),
            (wire(seq=2, ms=20, dropped=7), 70),
        ]))
        self.assertEqual([item.time_ms for item in recording.records], [0, 50, 60])
        self.assertEqual(recording.stats["clock_resets"], 1)
        self.assertEqual(recording.stats["sessions"], 2)
        self.assertEqual(recording.stats["device_drops_before_capture"], 5)
        self.assertEqual(recording.stats["device_drops"], 2)

    def test_raw_mixed_sessions_without_host_time_stay_ordered(self):
        recording = load_recording(io.BytesIO(line(wire(0, 100)) + b"diagnostic\n" + line(wire(0, 10, "bb"))))
        self.assertEqual([item.time_ms for item in recording.records], [0, 1])
        self.assertEqual(recording.stats["reboots"], 1)

    def test_playback_selection_equal_times_and_validation(self):
        recording = load_recording(journal([
            (wire(0, 0, kind="meta"), 0), (wire(1, 10, kind="input"), 10),
            (wire(2, 10), 10), (wire(3, 30), 30),
        ]))
        self.assertEqual([item.record["seq"] for item in playback_records(recording, 10, 20)], [1, 2, 3])
        self.assertEqual([item.record["seq"] for item in playback_records(recording, 10, 0)], [1, 2])
        self.assertEqual(list(playback_records(recording, 40)), [])
        for start, duration in ((-1, None), (float("nan"), None), (0, -1), (0, float("inf"))):
            with self.assertRaises(ValueError):
                list(playback_records(recording, start, duration))

    def test_demo_covers_ui_states_and_is_deterministic(self):
        values = list(demo_records())
        self.assertEqual(values, list(demo_records()))
        recording = load_recording(journal([(item.record, item.host_ms) for item in values]))
        self.assertEqual(recording.duration_ms, 15000)
        for key in ("speedScreen", "hudSelector", "elevation", "pending", "moving", "statusFault"):
            self.assertTrue(any(item.record["view"][key] for item in recording.views), key)


class CliTests(unittest.TestCase):
    def cli(self, *arguments, **kwargs):
        return subprocess.run([sys.executable, str(ROOT / "tools/crowpanel_trace.py"), *map(str, arguments)],
                              capture_output=True, text=True, timeout=10, **kwargs)

    def test_file_record_inspect_replay_and_overwrite_protection(self):
        with tempfile.TemporaryDirectory() as directory:
            raw, recorded = Path(directory) / "raw.txt", Path(directory) / "session.jsonl"
            corrupt = json.dumps({"schema": JOURNAL_SCHEMA, "v": 1, "host_ms": 10 ** 1000, "record": wire()}).encode() + b"\n"
            corrupt += json.dumps(wire(view={"status": "\ud800"})).encode() + b"\n"
            raw.write_bytes(b"hello\n" + corrupt + line(wire(0, 100, kind="meta")) + line(wire(2, 150)) + b"@CPTRACE {bad}\npartial")
            result = self.cli("record", "--input", raw, "--output", recorded)
            self.assertEqual(result.returncode, 0, result.stderr)
            summary = json.loads(result.stderr)
            self.assertEqual(summary["stopped"], "eof")
            self.assertEqual(summary["sequence_gaps"], 1)
            self.assertEqual(summary["malformed_lines"], 1)
            self.assertEqual(summary["invalid_records"], 2)
            self.assertEqual(summary["truncated_lines"], 1)
            saved = recorded.read_bytes()
            again = self.cli("record", "--input", raw, "--output", recorded)
            self.assertNotEqual(again.returncode, 0)
            self.assertEqual(saved, recorded.read_bytes())
            info = self.cli("inspect", recorded, "--json")
            self.assertEqual(json.loads(info.stdout)["duration_ms"], 50)
            replay = self.cli("replay", recorded, "--speed", "1000", "--start", "0.01", "--duration", "0.04")
            self.assertEqual(replay.returncode, 0, replay.stderr)
            self.assertEqual([json.loads(row)["seq"] for row in replay.stdout.splitlines()], [2])

    def test_invalid_speed_and_empty_inspection(self):
        with tempfile.TemporaryDirectory() as directory:
            empty = Path(directory) / "empty.jsonl"
            empty.touch()
            self.assertEqual(self.cli("inspect", empty).returncode, 1)
            for bad in ("0", "nan", "inf", "-1"):
                self.assertEqual(self.cli("replay", empty, "--speed", bad).returncode, 2)

    @unittest.skipUnless(os.name == "posix", "POSIX serial integration")
    def test_serial_disconnect_retains_complete_records_and_reports_partial_tail(self):
        import pty
        import termios
        with tempfile.TemporaryDirectory() as directory:
            master, slave = pty.openpty()
            output = Path(directory) / "disconnect.jsonl"
            process = subprocess.Popen([sys.executable, str(ROOT / "tools/crowpanel_trace.py"), "record",
                                        "--port", os.ttyname(slave), "--output", str(output)],
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                deadline = time.monotonic() + 2
                while termios.tcgetattr(slave)[3] & termios.ICANON:
                    if process.poll() is not None or time.monotonic() > deadline:
                        self.fail("recorder did not configure raw serial input")
                    time.sleep(0.01)
                os.write(master, line(wire()) + WIRE_PREFIX + b'{"incomplete":')
                while not output.exists() or not output.stat().st_size:
                    if time.monotonic() > deadline:
                        self.fail("recorder did not save received bytes")
                    time.sleep(0.01)
                os.close(master)
                master = None
                _, stderr = process.communicate(timeout=5)
                self.assertEqual(process.returncode, 1, stderr)
                summary = json.loads(stderr.splitlines()[-1])
                self.assertEqual(summary["stopped"], "disconnected")
                self.assertEqual(summary["truncated_lines"], 1)
                self.assertEqual(len(load_recording(output).records), 1)
                self.assertNotIn("Traceback", stderr)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.communicate()
                if master is not None:
                    os.close(master)
                os.close(slave)

    @unittest.skipUnless(os.name == "posix", "POSIX serial integration")
    def test_pty_recording_duration_and_interrupt_never_send_bytes(self):
        import pty
        import termios
        for interrupted in (False, True):
            with self.subTest(interrupted=interrupted), tempfile.TemporaryDirectory() as directory:
                master, slave = pty.openpty()
                output = Path(directory) / "pty.jsonl"
                process = subprocess.Popen([sys.executable, str(ROOT / "tools/crowpanel_trace.py"), "record",
                                            "--port", os.ttyname(slave), "--output", str(output),
                                            "--duration", "2" if interrupted else "0.4"],
                                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                try:
                    deadline = time.monotonic() + 2
                    while termios.tcgetattr(slave)[3] & termios.ICANON:
                        if process.poll() is not None or time.monotonic() > deadline:
                            self.fail("recorder did not configure raw serial input")
                        time.sleep(0.01)
                    raw = b"diagnostic\n" + line(wire(0, 100, kind="meta")) + line(wire(1, 110))
                    for offset in range(0, len(raw), 7):
                        os.write(master, raw[offset:offset + 7])
                    deadline = time.monotonic() + 1
                    while not output.exists() or output.stat().st_size == 0:
                        if time.monotonic() > deadline:
                            self.fail("recorder did not save received bytes")
                        time.sleep(0.01)
                    self.assertFalse(select.select([master], [], [], 0)[0], "recorder wrote bytes to device")
                    self.assertFalse(termios.tcgetattr(slave)[2] & termios.HUPCL)
                    if interrupted:
                        process.send_signal(signal.SIGINT)
                    stdout, stderr = process.communicate(timeout=5)
                    self.assertEqual(process.returncode, 0, stderr)
                    self.assertFalse(stdout)
                    summary = json.loads(stderr)
                    self.assertEqual(summary["stopped"], "interrupted" if interrupted else "duration")
                    recording = load_recording(output)
                    self.assertEqual([item.record["seq"] for item in recording.records], [0, 1])
                    self.assertEqual(recording.records[1].record["view"]["status"], "Движение · AZ")
                finally:
                    if process.poll() is None:
                        process.kill()
                        process.communicate()
                    os.close(master)
                    os.close(slave)


if __name__ == "__main__":
    unittest.main()
