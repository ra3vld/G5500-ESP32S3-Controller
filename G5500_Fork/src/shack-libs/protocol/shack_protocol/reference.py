# SPDX-License-Identifier: GPL-3.0-or-later
"""Reference discovery and tuner reads shared by the four transport bindings."""

from bisect import bisect_right
from dataclasses import dataclass
import math
import re
from typing import Callable
from urllib.parse import parse_qsl

from . import wire_pb2 as wire
from .codec import (
    BINDINGS, ID_PATTERN, MAX_CATALOG_PAGE, MAX_PACKET, VERSION_MAJOR,
    BindingError, decode_datagram, decode_json, encode_datagram, encode_json,
    frame_usb, valid_id, version,
)

_HTTP = BINDINGS["http"]
_MQTT = BINDINGS["mqtt"]
_MAX_TARGET = BINDINGS["limits"]["http_target_bytes"]
_STATUS_BY_ERROR = {
    wire.Error.INVALID_REQUEST: 400,
    wire.Error.UNSUPPORTED_VERSION: 400,
    wire.Error.NOT_FOUND: 404,
    wire.Error.UNAVAILABLE: 503,
}


def _http_route(call):
    method, route = _HTTP[call].split(" ", 1)
    return method, route.replace("{major}", str(VERSION_MAJOR))


def _mqtt_topic(direction, node_id, request_id):
    return _MQTT[direction].format(major=VERSION_MAJOR, node_id=node_id, request_id=request_id)


def _make_http_request(node_id, method, target, request_id, requested_version):
    if not target.isascii() or len(target) > _MAX_TARGET:
        raise BindingError("invalid HTTP request target")
    path, separator, query = target.partition("?")
    request = wire.Request(version=requested_version, node_id=node_id, request_id=request_id)
    if (method, path) == _http_route("hello") and not separator:
        request.hello.SetInParent()
        return request
    if (method, path) == _http_route("list_devices"):
        try:
            params = parse_qsl(query, keep_blank_values=True, strict_parsing=True, max_num_fields=2)
        except ValueError as error:
            raise BindingError("invalid query") from error
        values = dict(params)
        if len(values) != len(params) or values.keys() - {"after", "limit"}:
            raise BindingError("invalid query")
        limit = values.get("limit", "0")
        if not re.fullmatch(r"[0-9]{1,10}", limit) or int(limit) > 0xFFFFFFFF:
            raise BindingError("invalid catalog limit")
        request.list_devices.after_device_id = values.get("after", "")
        request.list_devices.limit = int(limit)
        return request
    state_method, state_path = _http_route("get_tuner_state")
    pattern = re.escape(state_path).replace(re.escape("{device_id}"), f"({ID_PATTERN})")
    device = re.fullmatch(pattern, path)
    if method == state_method and device and not separator:
        request.get_tuner_state.device_id = device[1]
        return request
    raise BindingError("unknown route")


@dataclass(frozen=True)
class TunerSource:
    capabilities: wire.TunerCapabilities
    snapshot: Callable[[], wire.TunerState | None]


class ReferenceStation:
    """A read-only station whose caller supplies transport I/O and live snapshots."""

    def __init__(self, node_id, tuners):
        if not valid_id(node_id):
            raise ValueError("invalid station identifier")
        self.node_id = node_id
        self._tuners = {}
        for device_id, source in tuners.items():
            if not valid_id(device_id) or source.capabilities.antenna_ports > 255:
                raise ValueError("invalid tuner descriptor")
            caps = wire.TunerCapabilities()
            caps.CopyFrom(source.capabilities)
            self._tuners[device_id] = TunerSource(caps, source.snapshot)
        self._ids = sorted(self._tuners)

    def dispatch(self, request):
        response = wire.Response(version=version(), request_id=request.request_id, node_id=self.node_id)
        if not valid_id(request.request_id) or not valid_id(request.node_id):
            raise BindingError("invalid message identifier")
        if request.node_id != self.node_id:
            response.error.code = wire.Error.NOT_FOUND
        elif request.version != response.version:
            response.error.code = wire.Error.UNSUPPORTED_VERSION
        elif request.HasField("hello"):
            response.hello.max_message_bytes = MAX_PACKET
            response.hello.max_catalog_page = MAX_CATALOG_PAGE
        elif request.HasField("list_devices"):
            self._list_devices(request.list_devices, response)
        elif request.HasField("get_tuner_state"):
            self._tuner_state(request.get_tuner_state.device_id, response)
        else:
            response.error.code = wire.Error.INVALID_REQUEST
        return response

    def _list_devices(self, query, response):
        if query.after_device_id and not valid_id(query.after_device_id):
            response.error.code = wire.Error.INVALID_REQUEST
            return
        limit = query.limit or MAX_CATALOG_PAGE
        if limit > MAX_CATALOG_PAGE:
            response.error.code = wire.Error.INVALID_REQUEST
            return
        catalog = response.catalog
        catalog.SetInParent()
        start = bisect_right(self._ids, query.after_device_id)
        end = start + limit
        for device_id in self._ids[start:end]:
            item = catalog.devices.add(device_id=device_id)
            item.tuner.CopyFrom(self._tuners[device_id].capabilities)
        if end < len(self._ids):
            catalog.next_after_device_id = self._ids[end - 1]

    def _tuner_state(self, device_id, response):
        if not valid_id(device_id):
            response.error.code = wire.Error.INVALID_REQUEST
            return
        source = self._tuners.get(device_id)
        if source is None:
            response.error.code = wire.Error.NOT_FOUND
            return
        snapshot = source.snapshot()
        if snapshot is None:
            response.error.code = wire.Error.UNAVAILABLE
            return
        state = response.tuner_state
        state.CopyFrom(snapshot)
        state.device_id = device_id
        if state.HasField("antenna") and state.antenna >= source.capabilities.antenna_ports:
            state.ClearField("antenna")
        if state.HasField("meter"):
            for field, value in state.meter.ListFields():
                if not math.isfinite(value):
                    state.meter.ClearField(field.name)
            if not state.meter.ListFields():
                state.ClearField("meter")

    def http(self, method, target, headers):
        normalized = {key.lower(): value for key, value in headers.items()}
        text_version = normalized.get(_HTTP["version_header"].lower(), "")
        matched = re.fullmatch(r"([0-9]{1,3})\.([0-9]{1,3})", text_version)
        if not matched:
            raise BindingError("invalid Shack-Version header")
        request_id = normalized.get(_HTTP["request_id_header"].lower(), "")
        requested_version = wire.Version(major=int(matched[1]), minor=int(matched[2]))
        request = _make_http_request(self.node_id, method, target, request_id, requested_version)
        response = self.dispatch(request)
        status = _STATUS_BY_ERROR[response.error.code] if response.HasField("error") else 200
        return status, encode_json(response)

    def usb(self, payload):
        """Consume one request payload from UsbFramer and produce a framed response."""
        frame = decode_json(payload, wire.UsbRequest())
        request = _make_http_request(
            self.node_id, frame.method, frame.target, frame.request_id, frame.version
        )
        return frame_usb(encode_json(self.dispatch(request)))

    def udp(self, packet):
        request = decode_datagram(packet, wire.Request())
        return encode_datagram(self.dispatch(request))

    def mqtt(self, topic, payload, *, retained):
        if retained:
            raise BindingError("retained request")
        request = decode_json(payload, wire.Request())
        if (request.node_id != self.node_id or not valid_id(request.request_id) or
                topic != _mqtt_topic("request", self.node_id, request.request_id)):
            raise BindingError("request does not match MQTT topic")
        response = self.dispatch(request)
        return _mqtt_topic("response", self.node_id, request.request_id), encode_json(response)
