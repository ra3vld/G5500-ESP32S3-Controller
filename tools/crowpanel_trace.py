#!/usr/bin/env python3
"""Read-only CrowPanel USB recorder, trace inspection and timed JSON playback."""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import json
import math
import os
from pathlib import Path
import select
import sys
import time

from crowpanel_trace_core import (Timeline, TraceDecoder, demo_records, journal_line,
                                 load_recording, playback_records)


def nonnegative(value: str) -> float:
    number = float(value)
    if not math.isfinite(number) or number < 0:
        raise argparse.ArgumentTypeError("must be finite and non-negative")
    return number


def positive(value: str) -> float:
    number = nonnegative(value)
    if number == 0:
        raise argparse.ArgumentTypeError("must be positive")
    return number


@contextmanager
def serial_reader(port: str, baud: int):
    """POSIX raw serial input; never write bytes or issue DTR/RTS ioctls.

    HUPCL is cleared during use and restoration so close does not deliberately
    lower the modem-control lines. No input flush is used: an already-streaming
    device may be attached without losing the first complete record.
    """
    if os.name != "posix":
        raise OSError("serial recording currently requires Linux/macOS; use --input for a captured stream")
    import termios
    speed = getattr(termios, "B" + str(baud), None)
    if speed is None:
        raise ValueError(f"unsupported serial baud rate: {baud}")
    fd = os.open(port, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
    original = None
    try:
        original = termios.tcgetattr(fd)
        attributes = list(original)
        attributes[6] = list(original[6])
        attributes[0] = 0
        attributes[1] = 0
        attributes[2] &= ~(termios.CSIZE | termios.PARENB | termios.CSTOPB | termios.HUPCL)
        attributes[2] |= termios.CS8 | termios.CREAD | termios.CLOCAL
        if hasattr(termios, "CRTSCTS"):
            attributes[2] &= ~termios.CRTSCTS
        attributes[3] = 0
        attributes[4] = attributes[5] = speed
        attributes[6][termios.VMIN] = 0
        attributes[6][termios.VTIME] = 0
        termios.tcsetattr(fd, termios.TCSANOW, attributes)
        yield fd
    except termios.error as error:
        raise OSError(f"cannot configure serial device {port}: {error}") from error
    finally:
        if original is not None:
            original[2] &= ~termios.HUPCL
            try:
                termios.tcsetattr(fd, termios.TCSANOW, original)
            except (OSError, termios.error):
                pass  # Disconnection must not hide the recording summary.
        os.close(fd)


@contextmanager
def source_reader(arguments):
    if arguments.port:
        with serial_reader(arguments.port, arguments.baud) as fd:
            yield fd
    elif arguments.input == "-":
        yield sys.stdin.buffer.fileno()
    else:
        fd = os.open(arguments.input, os.O_RDONLY | os.O_NONBLOCK)
        try:
            yield fd
        finally:
            os.close(fd)


def _report(stats: dict, duration_ms: float = 0, destination=sys.stderr):
    summary = dict(stats)
    summary["duration_ms"] = round(duration_ms, 3)
    print(json.dumps(summary, ensure_ascii=False, sort_keys=True), file=destination)


def record(arguments) -> int:
    output = Path(arguments.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    decoder, timeline = TraceDecoder(), Timeline()
    stopped, status = "eof", 0
    started = time.monotonic()
    end = started + arguments.duration if arguments.duration is not None else math.inf
    # Exclusive creation protects an earlier session from accidental overwrite.
    # Open the source first so invalid ports never leave an empty journal behind.
    with source_reader(arguments) as fd, output.open("x", encoding="utf-8", newline="\n") as journal:
        try:
            while True:
                now = time.monotonic()
                if now >= end:
                    stopped = "duration"
                    break
                ready, _, _ = select.select([fd], [], [], min(0.1, end - now))
                if not ready:
                    continue
                try:
                    data = os.read(fd, 16384)
                except BlockingIOError:
                    continue
                if not data:
                    if arguments.port:
                        stopped, status = "disconnected", 1
                    break
                host_ms = (time.monotonic() - started) * 1000
                accepted = decoder.feed(data)
                for value in accepted:
                    # Timestamp observed reception; do not inherit timestamps
                    # from arbitrary bytes received from the serial device.
                    journal.write(journal_line(value.record, host_ms))
                    timeline.append(value.record, host_ms)
                if accepted:
                    journal.flush()
        except KeyboardInterrupt:
            stopped = "interrupted"
        except OSError as error:
            stopped, status = "disconnected", 1
            print(f"Trace source disconnected: {error}", file=sys.stderr)
        finally:
            decoder.finish()
            journal.flush()
    stats = dict(decoder.stats + timeline.stats)
    stats["stopped"] = stopped
    stats["output"] = str(output)
    _report(stats, (time.monotonic() - started) * 1000)
    return status


def inspect_recording(arguments) -> int:
    recording = load_recording(arguments.file)
    stats = dict(recording.stats)
    stats["records"] = len(recording.records)
    stats["views"] = len(recording.views)
    stats["boots"] = list(dict.fromkeys(item.record["boot"] for item in recording.records))
    stats["metadata"] = [item.record for item in recording.records if item.record["type"] == "meta"]
    if arguments.json:
        _report(stats, recording.duration_ms, sys.stdout)
    else:
        print(f"{len(recording.records)} records, {len(recording.views)} views, {recording.duration_ms / 1000:.3f} s")
        _report(stats, recording.duration_ms, sys.stdout)
    return 0 if recording.records else 1


def replay(arguments) -> int:
    recording = load_recording(arguments.file)
    started = time.monotonic()
    duration = arguments.duration * 1000 if arguments.duration is not None else None
    try:
        for item in playback_records(recording, arguments.start * 1000, duration):
            target = started + (item.time_ms / 1000 - arguments.start) / arguments.speed
            # Small waits keep interruption responsive even across long pauses.
            while (remaining := target - time.monotonic()) > 0:
                time.sleep(min(remaining, 0.1))
            print(json.dumps(item.record, ensure_ascii=False, separators=(",", ":")), flush=True)
    except KeyboardInterrupt:
        return 130
    return 0 if recording.records else 1


def make_demo(arguments) -> int:
    output = Path(arguments.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x", encoding="utf-8", newline="\n") as journal:
        for value in demo_records():
            journal.write(journal_line(value.record, value.host_ms))
    print(f"Synthetic demonstration saved: {output}")
    return 0


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    commands = result.add_subparsers(dest="command", required=True)
    capture = commands.add_parser("record", help="record USB serial telemetry without sending device commands")
    source = capture.add_mutually_exclusive_group(required=True)
    source.add_argument("--port", help="serial device, preferably /dev/serial/by-id/...")
    source.add_argument("--input", help="record an existing wire stream; '-' reads stdin")
    capture.add_argument("--baud", type=int, default=115200)
    capture.add_argument("--output", required=True, help="new JSONL journal (never overwritten)")
    capture.add_argument("--duration", type=nonnegative, help="maximum recording seconds; Ctrl-C stops safely")
    capture.set_defaults(run=record)
    inspection = commands.add_parser("inspect", help="summarize validity, gaps, sessions and firmware metadata")
    inspection.add_argument("file")
    inspection.add_argument("--json", action="store_true")
    inspection.set_defaults(run=inspect_recording)
    playback = commands.add_parser("replay", help="replay accepted wire records to stdout at recorded intervals")
    playback.add_argument("file")
    playback.add_argument("--speed", type=positive, default=1.0)
    playback.add_argument("--start", type=nonnegative, default=0.0, help="start at this many seconds")
    playback.add_argument("--duration", type=nonnegative, help="recording seconds to replay before speed scaling")
    playback.set_defaults(run=replay)
    demo = commands.add_parser("demo", help="make a synthetic recording for trying playback and video export")
    demo.add_argument("--output", required=True)
    demo.set_defaults(run=make_demo)
    return result


def main(argv=None) -> int:
    arguments = parser().parse_args(argv)
    try:
        return arguments.run(arguments)
    except BrokenPipeError:
        return 0
    except (OSError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
