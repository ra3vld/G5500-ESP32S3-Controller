"""CrowPanel USB trace decoding and a device-clock playback timeline.

Public API: ``load_recording(path)`` returns ``Recording``. Its ``records`` retain
the complete wire dictionaries, with ``time_ms`` measured from the first record.
Equal-time records remain in received order. ``session`` changes at a boot/clock
reset; a uint32 millis wrap remains in the same session. Between sessions the
recorded host monotonic interval is used, when available. No device commands are
needed by this module or the recorder.
"""
from __future__ import annotations

from collections import Counter
from dataclasses import dataclass, field
import json
import math
from pathlib import Path
import re
from typing import BinaryIO, Iterable, Iterator

WIRE_PREFIX = b"@CPTRACE "
WIRE_SCHEMA = "crowpanel.trace"
JOURNAL_SCHEMA = "crowpanel.recording"
MAX_LINE_BYTES = 65536
UINT32 = 1 << 32
UINT64 = 1 << 64
# Relative capture timestamps are milliseconds. This still permits more than
# 285,000 years while keeping malicious JSON integers away from float overflow.
MAX_HOST_MS = 1 << 53


def _uint(value: object, bits: int = 32) -> bool:
    return type(value) is int and 0 <= value < (1 << bits)


def _finite_json(value: object) -> bool:
    if isinstance(value, float):
        return math.isfinite(value)
    if isinstance(value, dict):
        return all(_finite_json(key) and _finite_json(item) for key, item in value.items())
    if isinstance(value, list):
        return all(_finite_json(item) for item in value)
    if isinstance(value, str):
        try:
            value.encode("utf-8")
        except UnicodeError:
            return False
    return True


def _host_timestamp(value: object) -> bool:
    return type(value) in (int, float) and 0 <= value <= MAX_HOST_MS


def _touch_action(value: object) -> bool:
    return _uint(value, 8) and value <= 6


def validate_record(record: object) -> bool:
    """Validate the v1 framing; retain unknown fields for future diagnostics.

    View fields are checked by the renderer against its matching firmware UI.
    This decoder intentionally preserves partial/future view dictionaries too.
    """
    if not isinstance(record, dict) or not _finite_json(record):
        return False
    if record.get("schema") != WIRE_SCHEMA or type(record.get("v")) is not int or record["v"] != 1:
        return False
    if not isinstance(record.get("boot"), str) or not re.fullmatch(r"[0-9a-fA-F]{1,64}", record["boot"]):
        return False
    if not _uint(record.get("seq"), 64) or not _uint(record.get("ms")):
        return False
    if "dropped" in record and not _uint(record["dropped"], 64):
        return False
    if "pressed" in record and not _touch_action(record["pressed"]):
        return False
    kind = record.get("type")
    if kind == "view":
        return isinstance(record.get("view"), dict) and _touch_action(record.get("pressed"))
    if kind == "input":
        return isinstance(record.get("input"), dict)
    return kind == "meta"


@dataclass
class DecodedRecord:
    record: dict
    host_ms: float | None = None


class TraceDecoder:
    """Bounded incremental byte decoder for serial data and recording journals.

    ``feed`` accepts arbitrary byte boundaries, including inside UTF-8 text.
    Non-trace serial diagnostics are ignored. Only newline-terminated records
    are accepted: interrupted final writes are reported rather than replayed.
    """
    def __init__(self, max_line_bytes: int = MAX_LINE_BYTES):
        if max_line_bytes < len(WIRE_PREFIX) + 1:
            raise ValueError("max_line_bytes is too small")
        self.max_line_bytes = max_line_bytes
        self.stats: Counter = Counter()
        self._buffer = bytearray()
        self._discarding = False

    def _line(self, raw: bytes) -> DecodedRecord | None:
        self.stats["lines"] += 1
        raw = raw.rstrip(b"\r")
        if raw.startswith(WIRE_PREFIX):
            payload = raw[len(WIRE_PREFIX):]
        elif raw.startswith(b"{"):
            payload = raw
        else:
            self.stats["diagnostic_lines"] += 1
            return None
        try:
            value = json.loads(payload.decode("utf-8"))
        except (ValueError, UnicodeDecodeError, RecursionError):
            self.stats["malformed_lines"] += 1
            return None
        host_ms = None
        if isinstance(value, dict) and value.get("schema") == JOURNAL_SCHEMA:
            host_ms = value.get("host_ms")
            if (type(value.get("v")) is not int or value["v"] != 1 or
                    not _host_timestamp(host_ms)):
                self.stats["invalid_records"] += 1
                return None
            value = value.get("record")
        try:
            valid = validate_record(value)
        except RecursionError:
            valid = False
        if not valid:
            self.stats["invalid_records"] += 1
            return None
        self.stats["accepted_records"] += 1
        return DecodedRecord(value, host_ms)

    def feed(self, data: bytes) -> list[DecodedRecord]:
        result = []
        # split() allocates at most the caller's input chunk, never an unbounded
        # line. The retained buffer itself is bounded by max_line_bytes.
        fragments = data.split(b"\n")
        for index, fragment in enumerate(fragments):
            complete = index < len(fragments) - 1
            if not self._discarding:
                if len(self._buffer) + len(fragment) > self.max_line_bytes:
                    self._buffer.clear()
                    self._discarding = True
                    self.stats["oversized_lines"] += 1
                else:
                    self._buffer.extend(fragment)
            if complete:
                if not self._discarding:
                    decoded = self._line(bytes(self._buffer))
                    if decoded is not None:
                        result.append(decoded)
                self._buffer.clear()
                self._discarding = False
        return result

    def finish(self) -> None:
        if self._buffer or self._discarding:
            self.stats["truncated_lines"] += 1
        self._buffer.clear()
        self._discarding = False


@dataclass
class TimelineRecord:
    time_ms: float
    record: dict
    session: int
    host_ms: float | None = None


@dataclass
class Recording:
    records: list[TimelineRecord] = field(default_factory=list)
    stats: dict = field(default_factory=dict)

    @property
    def duration_ms(self) -> float:
        return self.records[-1].time_ms if self.records else 0.0

    @property
    def views(self) -> list[TimelineRecord]:
        return [item for item in self.records if item.record["type"] == "view"]


class Timeline:
    """Incremental timeline builder shared by files, recording and live views."""
    def __init__(self):
        self.stats: Counter = Counter()
        self._last: TimelineRecord | None = None
        self._anchor: TimelineRecord | None = None
        self._dropped = 0

    def _observe_drops(self, record: dict, new_boot: bool) -> None:
        if new_boot:
            # Cumulative drops start at boot, not at recorder attachment.
            self._dropped = record.get("dropped", 0)
            self.stats["device_drops_before_capture"] += self._dropped
        elif "dropped" in record:
            delta = (record["dropped"] - self._dropped) % UINT64
            if delta < UINT64 // 2:
                self.stats["device_drops"] += delta
                self._dropped = record["dropped"]
            else:
                self.stats["drop_counter_resets"] += 1
                self._dropped = record["dropped"]

    def append(self, record: dict, host_ms: float | None = None) -> TimelineRecord:
        last = self._anchor
        session = last.session if last else 0
        time_ms = self._last.time_ms if self._last else 0.0
        new_boot = last is None or record["boot"] != last.record["boot"]
        reset = new_boot
        clock_delta = 0
        sequence_delta = 1
        stale = False
        if last and not reset:
            clock_delta = (record["ms"] - last.record["ms"]) % UINT32
            sequence_delta = (record["seq"] - last.record["seq"]) % UINT64
            # Preserve stale packets in observed order without moving the
            # clock/sequence anchor, which would inflate the next interval.
            if sequence_delta >= UINT64 // 2:
                self.stats["out_of_order"] += 1
                stale = True
                clock_delta = 0
            elif clock_delta >= UINT32 // 2:
                reset = True
                self.stats["clock_resets"] += 1
            elif record["ms"] < last.record["ms"]:
                self.stats["millis_wraps"] += 1
        if reset:
            if last:
                session += 1
                self.stats["reboots"] += record["boot"] != last.record["boot"]
                gap = 1.0
                if host_ms is not None and last.host_ms is not None:
                    gap = max(0.0, host_ms - last.host_ms)
                time_ms += gap
            self.stats["sessions"] += 1
        elif not stale:
            time_ms += clock_delta
            if sequence_delta == 0:
                self.stats["duplicate_sequences"] += 1
            elif sequence_delta < UINT64 // 2:
                self.stats["sequence_gaps"] += sequence_delta - 1
            else:
                self.stats["out_of_order"] += 1
        if not stale:
            self._observe_drops(record, new_boot)
        self.stats[record["type"] + "_records"] += 1
        result = TimelineRecord(float(time_ms), record, session, host_ms)
        self._last = result
        if not stale:
            self._anchor = result
        return result


def journal_line(record: dict, host_ms: float) -> str:
    """Encode a wire record losslessly inside a host-time JSONL envelope."""
    if not validate_record(record) or not _host_timestamp(host_ms):
        raise ValueError("invalid record or host timestamp")
    return json.dumps({"schema": JOURNAL_SCHEMA, "v": 1, "host_ms": round(host_ms, 6),
                       "record": record}, ensure_ascii=False, separators=(",", ":"), allow_nan=False) + "\n"


def load_recording(path: str | Path | BinaryIO) -> Recording:
    """Load a binary file/path; ignore damaged lines and report them in stats."""
    decoder, timeline = TraceDecoder(), Timeline()
    records = []
    owned = not hasattr(path, "read")
    stream = open(path, "rb") if owned else path
    try:
        while True:
            chunk = stream.read(16384)
            if not chunk:
                break
            if isinstance(chunk, str):
                chunk = chunk.encode("utf-8")
            for value in decoder.feed(chunk):
                records.append(timeline.append(value.record, value.host_ms))
    finally:
        if owned:
            stream.close()
    decoder.finish()
    return Recording(records, dict(decoder.stats + timeline.stats))


def playback_records(recording: Recording, start_ms: float = 0,
                     duration_ms: float | None = None) -> Iterator[TimelineRecord]:
    """Select records in [start, start + duration], retaining received order."""
    if not math.isfinite(start_ms) or start_ms < 0:
        raise ValueError("start must be finite and non-negative")
    if duration_ms is not None and (not math.isfinite(duration_ms) or duration_ms < 0):
        raise ValueError("duration must be finite and non-negative")
    end = start_ms + duration_ms if duration_ms is not None else math.inf
    yield from (item for item in recording.records if start_ms <= item.time_ms <= end)


def demo_records() -> Iterable[DecodedRecord]:
    """A synthetic trace for trying the tools without connecting hardware."""
    boot, seq = "de000001", 0
    def record(kind: str, ms: int, **fields) -> DecodedRecord:
        nonlocal seq
        value = dict(schema=WIRE_SCHEMA, v=1, type=kind, boot=boot, seq=seq, ms=ms, dropped=0)
        value.update(fields)
        seq += 1
        return DecodedRecord(value, float(ms))
    yield record("meta", 0, firmware="synthetic-demo", build="synthetic-demo", width=480, height=480)
    view = dict(hud=True, hudSelector=False, hudName="Демонстрация", hudAddress="hud:192.168.1.42",
                hudRows=["Мачта · G-5500\n120 / 30 · Стоп · 0s", "Дача · G-800DXA\n45 / 0 · Стоп · 0s",
                         "Спутниковая\n240 / 80 · Нет связи · 12s"],
                dualAxis=True, speedScreen=False, speedAvailable=[True, True], speedPercent=[60, 45],
                elevation=False, feedback=True, targetValid=True, uncalibrated=False, secondaryUncalibrated=False,
                pending=False, moving=False, autoMode=True, autoHour=False, direction=0,
                current=30., target=30., minimum=0., maximum=450., secondaryCurrent=20., secondaryTarget=20.,
                secondaryFeedback=True, secondaryTargetValid=True, secondaryPending=False,
                azDirection=0, elDirection=0, sourceId=1, wifiConnected=True, wifiAp=False, wifiRssi=-58,
                bluetoothEnabled=False, bluetoothConnected=False, ip="STA: 192.168.1.42", heap="Heap: 123 КБ",
                statusFault=False, status="Остановлен")
    for ms in range(0, 15001, 50):
        v = dict(view)
        v["speedPercent"] = list(view["speedPercent"])
        pressed = 0
        if 1000 <= ms < 1500:
            v.update(target=210., pending=True, status="Подтвердите цель")
            pressed = 3 if ms >= 1400 else 0
        elif 1500 <= ms < 7000:
            current = min(210., 30. + (ms - 1500) / 5500 * 180)
            v.update(current=current, target=210., moving=True, direction=1, azDirection=1, status="Поворот по цели")
        elif 7000 <= ms < 9000:
            v.update(elevation=True, maximum=180.,
                     current=20. + (ms - 7000) / 2000 * 40, target=60., secondaryCurrent=210., secondaryTarget=210.,
                     direction=1, elDirection=1, moving=True, status="Поворот по цели")
        elif 9000 <= ms < 11000:
            v.update(current=210., target=210., secondaryCurrent=60., secondaryTarget=60., speedScreen=True)
            v["speedPercent"] = [round(20 + (ms - 9000) / 2000 * 70), 45]
            pressed = 6
        elif 11000 <= ms < 13000:
            v.update(current=210., target=210., secondaryCurrent=60., secondaryTarget=60., hudSelector=True)
        elif ms >= 13000:
            v.update(current=210., target=210., secondaryCurrent=60., secondaryTarget=60., wifiConnected=False,
                     statusFault=True, status="Нет связи с приводом")
        yield record("view", ms, view=v, pressed=pressed)
