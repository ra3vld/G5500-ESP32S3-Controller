# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared message limits, codecs and USB stream framing."""

import json
from pathlib import Path
import re

from google.protobuf import json_format
from google.protobuf.message import DecodeError

from . import wire_pb2 as wire

BINDINGS = json.loads((Path(__file__).resolve().parents[1] / "bindings.json").read_text())
VERSION_MAJOR = BINDINGS["version"]["major"]
VERSION_MINOR = BINDINGS["version"]["minor"]
MAX_PACKET = BINDINGS["limits"]["message_bytes"]
MAX_CATALOG_PAGE = BINDINGS["limits"]["catalog_page"]
MAX_ID = BINDINGS["limits"]["id_bytes"]
ID_PATTERN = r"[A-Za-z0-9][A-Za-z0-9._-]{0,%d}" % (MAX_ID - 1)
USB_PREFIX = BINDINGS["usb"]["prefix"].encode("ascii")
USB_HEADER_LIMIT = len(USB_PREFIX) + len(str(MAX_PACKET))


class BindingError(ValueError):
    """The transport cannot route or decode this message."""


def version():
    return wire.Version(major=VERSION_MAJOR, minor=VERSION_MINOR)


def valid_id(value):
    return re.fullmatch(ID_PATTERN, value) is not None


def bounded(payload):
    if not 0 < len(payload) <= MAX_PACKET:
        raise BindingError("invalid message size")
    return payload


def encode_json(message):
    data = json_format.MessageToDict(message)
    return bounded(json.dumps(data, separators=(",", ":"), ensure_ascii=False).encode("utf-8"))


def decode_json(payload, message):
    try:
        return json_format.Parse(
            bounded(payload).decode("utf-8"), message, max_recursion_depth=8
        )
    except (UnicodeDecodeError, RecursionError, json_format.ParseError) as error:
        raise BindingError("invalid ProtoJSON") from error


def encode_datagram(message):
    return bounded(message.SerializeToString(deterministic=True))


def decode_datagram(payload, message):
    try:
        message.ParseFromString(bounded(payload))
    except DecodeError as error:
        raise BindingError("invalid Protobuf datagram") from error
    return message


def frame_usb(payload):
    return USB_PREFIX + str(len(bounded(payload))).encode("ascii") + b"\n" + payload


class UsbFramer:
    """Decode bounded frames from arbitrary serial chunks."""

    def __init__(self):
        self.reset()

    def reset(self):
        self._pending = bytearray()
        self._remaining = 0

    def feed(self, chunk):
        frames = []
        for byte in chunk:
            if self._remaining:
                self._pending.append(byte)
                self._remaining -= 1
                if self._remaining == 0:
                    frames.append(bytes(self._pending))
                    self._pending.clear()
                continue
            if byte == ord("\n"):
                header = bytes(self._pending)
                self._pending.clear()
                length = header[len(USB_PREFIX):]
                if not header.startswith(USB_PREFIX) or not re.fullmatch(rb"[1-9][0-9]*", length):
                    raise BindingError("invalid USB frame header")
                self._remaining = int(length)
                if self._remaining > MAX_PACKET:
                    self.reset()
                    raise BindingError("USB frame exceeds message limit")
            else:
                if len(self._pending) >= USB_HEADER_LIMIT:
                    self.reset()
                    raise BindingError("USB frame header is too long")
                self._pending.append(byte)
        return frames
