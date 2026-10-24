#!/usr/bin/env python3
"""Replay/renderer regression checks; native checks use the built trace backend."""
import copy
import struct
import unittest
import zlib

from crowpanel_trace_core import Recording, TimelineRecord, demo_records
from crowpanel_trace_viewer import (FRAME_BYTES, INPUT_SIZE, NativeRenderer, Replay, ROOT,
                                   input_command, png_frame, view_command)


def state(seq, ms, current=0, **changes):
    record = {"schema": "crowpanel.trace", "v": 1, "boot": "abc1", "seq": seq,
              "ms": ms, "type": "view", "pressed": 0,
              "view": {"feedback": True, "current": current, "wifiConnected": True}}
    record["view"].update(changes)
    return record


def timeline(*records):
    return Recording([TimelineRecord(record["ms"], record, 0) for record in records])


def input_record(seq, ms, kind, **values):
    record = state(seq, ms)
    record.update(type="input", input=dict(kind=kind, healthy=True, down=False))
    record["input"].update(values)
    return record


class FakeRenderer:
    def __init__(self):
        self.calls = []

    def reset(self):
        self.calls.append(("reset",))

    def update(self, ms, record):
        self.calls.append(("view", ms, record["seq"]))

    def pressed(self, ms, action):
        self.calls.append(("pressed", ms, action))

    def input(self, ms, record):
        self.calls.append(("input", ms, record["input"]["kind"]))

    def clear_inputs(self, ms):
        self.calls.append(("clear_inputs", ms))

    def frame(self, ms, show_inputs=False):
        self.calls.append(("frame", ms, True) if show_inputs else ("frame", ms))
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

    def test_input_wire_and_malformed_events(self):
        touch = input_record(1, 10, "touch", x=123, y=234)
        touch["input"]["down"] = True
        self.assertEqual(input_command(10, touch), b"TOUCH\t10\t1\t1\t123\t234\n")
        self.assertEqual(input_command(20, input_record(2, 20, "knob", delta=-3)), b"KNOB\t20\t1\t0\t-3\n")
        for key, value in (("x", -1), ("y", 480), ("healthy", 1), ("down", "true")):
            damaged = copy.deepcopy(touch)
            damaged["input"][key] = value
            self.assertEqual(input_command(20, damaged), b"CLEAR_INPUTS\t20\n")
        self.assertIsNone(input_command(0, {"input": {"kind": "future-input"}}))

    def test_record_gaps_clear_held_input_and_stale_steps_are_ignored(self):
        renderer = FakeRenderer()
        down = input_record(1, 10, "knob", delta=1)
        down["input"]["down"] = True
        last = state(4, 30)
        replay = Replay(timeline(state(0, 0), down, down, last), renderer)
        replay.frame(40, show_inputs=True)
        self.assertEqual([call for call in renderer.calls if call[0] == "input"], [("input", 10, "knob")])
        self.assertIn(("clear_inputs", 30), renderer.calls)
        self.assertEqual(renderer.calls[-1], ("frame", 40, True))

    def test_annotated_png_dimensions(self):
        png = png_frame(bytes(INPUT_SIZE * INPUT_SIZE * 3), INPUT_SIZE, INPUT_SIZE)
        self.assertEqual(struct.unpack(">II", png[16:24]), (600, 600))


RENDERER = ROOT / "build/crowpanel-trace/renderer"


@unittest.skipUnless(RENDERER.is_file(), "Build tools/build_crowpanel_trace_renderer.sh first")
class NativeTests(unittest.TestCase):
    @staticmethod
    def pixel(frame, x, y, size=INPUT_SIZE):
        offset = (y * size + x) * 3
        return frame[offset:offset + 3]

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

    def test_touch_release_retains_coordinates_then_fades_without_altering_plain_screen(self):
        with NativeRenderer(RENDERER) as renderer:
            renderer.update(0, state(0, 0))
            plain = renderer.frame(0)
            initial = renderer.frame(0, show_inputs=True)
            down = input_record(1, 10, "touch", x=200, y=220)
            down["input"]["down"] = True
            renderer.input(10, down)
            tapped = renderer.frame(10, show_inputs=True)
            self.assertEqual(len(tapped), 600 * 600 * 3)
            self.assertNotEqual(self.pixel(initial, 260, 280), self.pixel(tapped, 260, 280))
            renderer.input(20, input_record(2, 20, "touch", x=0, y=0))
            released = renderer.frame(30, show_inputs=True)
            self.assertNotEqual(self.pixel(initial, 260, 280), self.pixel(released, 260, 280))
            self.assertEqual(self.pixel(initial, 60, 60), self.pixel(released, 60, 60))
            self.assertEqual(renderer.frame(800, show_inputs=True), initial)
            self.assertEqual(renderer.frame(800), plain)

    def test_swipe_path_and_encoder_rotation_press_and_release(self):
        with NativeRenderer(RENDERER) as renderer:
            renderer.update(0, state(0, 0))
            initial = renderer.frame(0, show_inputs=True)
            for seq, (ms, x, y) in enumerate(((10, 150, 200), (20, 230, 250), (30, 300, 200)), 1):
                event = input_record(seq, ms, "touch", x=x, y=y)
                event["input"]["down"] = True
                renderer.input(ms, event)
            swiped = renderer.frame(35, show_inputs=True)
            self.assertNotEqual(self.pixel(initial, 250, 285), self.pixel(swiped, 250, 285))
            renderer.input(40, input_record(4, 40, "touch", x=0, y=0))
            renderer.input(50, input_record(5, 50, "knob", delta=2))
            rotated = renderer.frame(800, show_inputs=True)
            self.assertNotEqual(self.pixel(initial, 300, 20), self.pixel(rotated, 300, 20))
            renderer.input(810, input_record(6, 810, "knob", delta=-2))
            self.assertEqual(renderer.frame(1300, show_inputs=True), initial)
            pressed = input_record(7, 1310, "knob", delta=0)
            pressed["input"]["down"] = True
            renderer.input(1310, pressed)
            pushed = renderer.frame(1410, show_inputs=True)
            self.assertNotEqual(self.pixel(initial, 300, 30), self.pixel(pushed, 300, 30))
            renderer.input(1420, input_record(8, 1420, "knob", delta=0))
            self.assertEqual(renderer.frame(1550, show_inputs=True), initial)

    def test_input_animation_seek_and_output_cadence_are_deterministic(self):
        down = input_record(2, 27, "touch", x=160, y=240)
        down["input"]["down"] = True
        move = copy.deepcopy(down)
        move.update(seq=3, ms=59)
        move["input"].update(x=270, y=250)
        recording = timeline(state(0, 0), input_record(1, 11, "knob", delta=1), down, move,
                             input_record(4, 93, "touch", x=0, y=0),
                             input_record(5, 117, "knob", delta=-2), state(6, 500))
        with NativeRenderer(RENDERER) as renderer:
            replay = Replay(recording, renderer)
            replay.frame(34, show_inputs=True)
            replay.frame(99, show_inputs=True)
            intermediate = replay.frame(173, show_inputs=True)
            replay.frame(0)
            self.assertEqual(replay.frame(173, show_inputs=True), intermediate)

    def test_input_samples_do_not_change_the_plain_lvgl_animation(self):
        views = [state(0, 0, 0), state(2, 53, 90), state(4, 109, 180), state(6, 191, 270)]
        events = [input_record(1, 11, "knob", delta=1),
                  input_record(3, 59, "touch", down=True, x=270, y=250),
                  input_record(5, 117, "touch", x=0, y=0)]
        combined = sorted(views + events, key=lambda record: record["ms"])
        with NativeRenderer(RENDERER) as renderer:
            plain = Replay(timeline(*views), renderer).frame(207)
            renderer.reset()
            with_inputs = Replay(timeline(*combined), renderer).frame(207)
            self.assertEqual(plain, with_inputs)

    def test_loss_keeps_known_rotation_and_fades_observed_swipe_segments(self):
        with NativeRenderer(RENDERER) as renderer:
            renderer.update(0, state(0, 0))
            renderer.input(10, input_record(1, 10, "knob", delta=2))
            expected = renderer.frame(200, show_inputs=True)
            renderer.reset()
            renderer.update(0, state(0, 0))
            renderer.input(10, input_record(1, 10, "knob", delta=2))
            renderer.clear_inputs(20)
            self.assertEqual(renderer.frame(200, show_inputs=True), expected)
            renderer.reset()
            renderer.update(0, state(0, 0))
            initial = renderer.frame(0, show_inputs=True)
            renderer.input(10, input_record(1, 10, "touch", down=True, x=150, y=200))
            renderer.input(20, input_record(2, 20, "touch", down=True, x=230, y=250))
            renderer.clear_inputs(30)
            known = renderer.frame(100, show_inputs=True)
            self.assertNotEqual(self.pixel(initial, 250, 285), self.pixel(known, 250, 285))
            renderer.reset()
            renderer.update(0, state(0, 0))
            renderer.input(10, input_record(1, 10, "touch", down=True, x=150, y=200))
            renderer.input(20, input_record(2, 20, "touch", down=True, x=230, y=250))
            renderer.input(30, input_record(3, 30, "touch", x=0, y=0))
            self.assertNotEqual(renderer.frame(100, show_inputs=True), known)
            self.assertEqual(renderer.frame(800, show_inputs=True), initial)


if __name__ == "__main__":
    unittest.main()
