#!/usr/bin/env python3
"""Replay/renderer regression checks; native checks use the built trace backend."""
import copy
import struct
import unittest
import zlib

from crowpanel_trace_core import Recording, TimelineRecord, demo_records
from crowpanel_trace_viewer import (FRAME_BYTES, NativeRenderer, Replay, ROOT, png_frame, view_command)


def state(seq, ms, current=0, **changes):
    record = {"schema": "crowpanel.trace", "v": 1, "boot": "abc1", "seq": seq,
              "ms": ms, "type": "view", "pressed": 0,
              "view": {"feedback": True, "current": current, "wifiConnected": True}}
    record["view"].update(changes)
    return record


def timeline(*records):
    return Recording([TimelineRecord(record["ms"], record, 0) for record in records])


class FakeRenderer:
    def __init__(self):
        self.calls = []

    def reset(self):
        self.calls.append(("reset",))

    def update(self, ms, record):
        self.calls.append(("view", ms, record["seq"]))

    def pressed(self, ms, action):
        self.calls.append(("pressed", ms, action))

    def frame(self, ms):
        self.calls.append(("frame", ms))
        return b"frame"


class ReplayTests(unittest.TestCase):
    def test_frames_apply_intermediate_events(self):
        renderer = FakeRenderer()
        replay = Replay(timeline(state(0, 0), state(1, 11, 10), state(2, 19, 20)), renderer)
        replay.frame(33)
        self.assertEqual(renderer.calls, [("reset",), ("view", 0, 0), ("view", 11, 1),
                                          ("view", 19, 2), ("frame", 33)])

    def test_backwards_seek_reconstructs_from_start(self):
        renderer = FakeRenderer()
        replay = Replay(timeline(state(0, 0), state(1, 50, 10)), renderer)
        replay.frame(100)
        renderer.calls.clear()
        replay.frame(25)
        self.assertEqual(renderer.calls[-2:], [("view", 0, 0), ("frame", 25)])

    def test_reboot_resets_native_clock(self):
        renderer = FakeRenderer()
        recording = timeline(state(100, 0), state(101, 50))
        recording.records.append(TimelineRecord(700, state(0, 2), 1))
        replay = Replay(recording, renderer)
        replay.frame(750)
        self.assertEqual(renderer.calls[-3:], [("reset",), ("view", 0, 0), ("frame", 50)])

    def test_short_input_pressed_pulse_between_views(self):
        renderer = FakeRenderer()
        down = dict(state(1, 7), type="input", input={"touch": True}, pressed=1)
        up = dict(state(2, 12), type="input", input={"touch": False}, pressed=0)
        replay = Replay(timeline(state(0, 0), down, up, state(3, 50)), renderer)
        replay.frame(10)
        self.assertEqual(renderer.calls[-2:], [("pressed", 7, 1), ("frame", 10)])
        replay.frame(20)
        self.assertEqual(renderer.calls[-2:], [("pressed", 12, 0), ("frame", 20)])

    def test_stale_and_duplicate_views_do_not_replace_newer_view(self):
        renderer = FakeRenderer()
        replay = Replay(timeline(state(100, 0), state(101, 10), state(100, 10), state(101, 10),
                                 state(102, 20)), renderer)
        replay.frame(30)
        self.assertEqual([call[2] for call in renderer.calls if call[0] == "view"], [100, 101, 102])

    def test_wire_encoding_preserves_unicode_arrays_and_uint64(self):
        record = state(1, 0, hudRows=["АЗ\nEL", "", "OK"], sourceId=(1 << 64) - 1,
                       speedAvailable=[True, False], target=None)
        command = view_command(15, record)
        self.assertIn(b"hudRows[0]=" + "АЗ\nEL".encode().hex().encode(), command)
        self.assertIn(b"sourceId=18446744073709551615", command)
        self.assertIn(b"speedAvailable[0]=1", command)
        self.assertIn(b"target=null", command)
        self.assertEqual(command.count(b"\n"), 1)

    def test_png_encodes_full_rgb_without_optional_packages(self):
        frame = bytes([2, 4, 8]) * (480 * 480)
        png = png_frame(frame)
        self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
        self.assertEqual(struct.unpack(">II", png[16:24]), (480, 480))
        offset, compressed = 8, bytearray()
        while offset < len(png):
            size = struct.unpack(">I", png[offset:offset + 4])[0]
            if png[offset + 4:offset + 8] == b"IDAT":
                compressed.extend(png[offset + 8:offset + 8 + size])
            offset += size + 12
        raw = zlib.decompress(compressed)
        self.assertEqual(raw[:4], b"\0\2\4\10")
        self.assertEqual(len(raw), FRAME_BYTES + 480)

    def test_invalid_field_types_are_rejected_without_changing_values(self):
        for key, value in (("current", "1"), ("feedback", 1), ("direction", None),
                           ("speedPercent", [5]), ("hudRows", ["one"]), ("ip", 1),
                           ("current", 10 ** 1000), ("target", float('inf')),
                           ("minimum", None), ("maximum", None)):
            with self.subTest(key=key), self.assertRaises(ValueError):
                view_command(0, state(0, 0, **{key: value}))


RENDERER = ROOT / "build/crowpanel-trace/renderer"


@unittest.skipUnless(RENDERER.is_file(), "Build tools/build_crowpanel_trace_renderer.sh first")
class NativeTests(unittest.TestCase):
    def test_actual_ui_and_round_display_mask(self):
        with NativeRenderer(RENDERER) as renderer:
            renderer.update(0, state(0, 0, 45))
            frame = renderer.frame(0)
            self.assertEqual(len(frame), FRAME_BYTES)
            self.assertEqual(frame[:3], b"\0\0\0")
            self.assertEqual(frame[-3:], b"\0\0\0")
            self.assertGreater(len(set(frame)), 20)

    def test_native_pressed_pulse_and_release(self):
        with NativeRenderer(RENDERER) as renderer:
            renderer.update(0, state(0, 0))
            stopped = renderer.frame(0)
            renderer.pressed(7, 1)
            pressed = renderer.frame(10)
            renderer.pressed(12, 0)
            released = renderer.frame(20)
            self.assertNotEqual(stopped, pressed)
            self.assertEqual(stopped, released)

    def test_all_firmware_fields_unicode_and_null_coordinates(self):
        view = next(item.record for item in demo_records() if item.record["type"] == "view")
        view = copy.deepcopy(view)
        view["view"].update(current=None, target=None, secondaryCurrent=None,
                            secondaryTarget=None, sourceId=(1 << 64) - 1)
        with NativeRenderer(RENDERER) as renderer:
            renderer.update(0, view)
            self.assertEqual(len(renderer.frame(25)), FRAME_BYTES)

    def test_animation_survives_seek_and_is_independent_of_output_frames(self):
        recording = timeline(state(0, 0, 0), state(1, 53, 90), state(2, 109, 180), state(3, 191, 270))
        with NativeRenderer(RENDERER) as renderer:
            replay = Replay(recording, renderer)
            replay.frame(17)
            replay.frame(76)
            replay.frame(143)
            intermediate = replay.frame(207)
            # Backwards seek rebuilds all events, then another seek crosses the
            # same animation restart times with a different output cadence.
            replay.frame(0)
            direct = replay.frame(207)
            self.assertEqual(intermediate, direct)
            settled = replay.frame(400)
            self.assertNotEqual(intermediate, settled)


if __name__ == "__main__":
    unittest.main()
