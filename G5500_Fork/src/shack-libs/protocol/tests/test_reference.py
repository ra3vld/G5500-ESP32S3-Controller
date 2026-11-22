# SPDX-License-Identifier: GPL-3.0-or-later
import json
from pathlib import Path
import unittest

from shack_protocol import wire_pb2 as wire
from shack_protocol.codec import (
    MAX_CATALOG_PAGE, MAX_PACKET, BindingError, UsbFramer,
    decode_datagram, decode_json, encode_datagram, encode_json, frame_usb, version,
)
from shack_protocol.reference import ReferenceStation, TunerSource

NODE = "shack-a"
DEVICE = "tuner.hf"
REQUEST = "req-1"
HTTP_HEADERS = {"Shack-Version": "1.0", "Shack-Request-Id": REQUEST}
MQTT_REQUEST = "shack/v1/shack-a/read/req-1/request"
MQTT_REPLY = "shack/v1/shack-a/read/req-1/reply"
STATE_PATH = "/shack/v1/devices/tuner.hf/state"


class ProtocolTest(unittest.TestCase):
    def setUp(self):
        self.snapshot = wire.TunerState(
            connected=True, sequence=42, mode=wire.TUNER_MODE_AUTO,
            antenna=0, meter_age_ms=0,
            meter=wire.RfMeter(forward_watts=0, reflected_watts=0, swr=1.3),
        )
        self.station = ReferenceStation(NODE, {
            DEVICE: TunerSource(
                wire.TunerCapabilities(tune=True, memory_tune=True, rf_meter=True, antenna_ports=2),
                lambda: self.snapshot,
            ),
        })

    def request(self, **call):
        return wire.Request(version=version(), request_id=REQUEST, node_id=NODE, **call)

    def state_request(self):
        return self.request(get_tuner_state=wire.GetTunerStateRequest(device_id=DEVICE))

    def through_all(self, path, request, http_status=200):
        status, plain = self.station.http("GET", path, HTTP_HEADERS)
        self.assertEqual(status, http_status)
        expected = decode_json(plain, wire.Response())
        self.assertEqual((expected.version, expected.node_id, expected.request_id),
                         (version(), NODE, REQUEST))

        serial = wire.UsbRequest(version=version(), request_id=REQUEST, method="GET", target=path)
        reader = UsbFramer()
        payloads = []
        for byte in frame_usb(encode_json(serial)):
            payloads.extend(reader.feed(bytes([byte])))
        self.assertEqual(len(payloads), 1)
        reply = UsbFramer().feed(self.station.usb(payloads[0]))
        self.assertEqual(reply, [plain])

        binary = self.station.udp(encode_datagram(request))
        self.assertEqual(decode_datagram(binary, wire.Response()), expected)
        self.assertLessEqual(len(binary), MAX_PACKET)

        topic, mqtt = self.station.mqtt(MQTT_REQUEST, encode_json(request), retained=False)
        self.assertEqual(topic, MQTT_REPLY)
        self.assertEqual(mqtt, plain)
        return expected

    def test_hello_through_all_bindings(self):
        response = self.through_all("/shack/v1/hello", self.request(hello=wire.HelloRequest()))
        self.assertEqual(response.hello.max_message_bytes, MAX_PACKET)
        self.assertEqual(response.hello.max_catalog_page, MAX_CATALOG_PAGE)

    def test_catalog_through_all_bindings(self):
        response = self.through_all("/shack/v1/devices?limit=1", self.request(
            list_devices=wire.ListDevicesRequest(limit=1)
        ))
        self.assertEqual(len(response.catalog.devices), 1)
        self.assertEqual(response.catalog.devices[0].device_id, DEVICE)
        self.assertTrue(response.catalog.devices[0].tuner.memory_tune)

    def test_tuner_state_through_all_bindings_preserves_zero(self):
        state = self.through_all(STATE_PATH, self.state_request()).tuner_state
        self.assertEqual(state.device_id, DEVICE)
        self.assertEqual(state.sequence, 42)
        self.assertTrue(state.HasField("antenna"))
        self.assertTrue(state.HasField("meter_age_ms"))
        self.assertTrue(state.meter.HasField("forward_watts"))
        self.assertEqual((state.antenna, state.meter_age_ms, state.meter.forward_watts), (0, 0, 0))

    def test_pagination_and_maximum_size(self):
        devices = [f"tuner.{i:026d}" for i in range(MAX_CATALOG_PAGE + 1)]
        caps = wire.TunerCapabilities(
            tune=True, memory_tune=True, full_tune=True, toggle_bypass=True, manual_mode=True,
            auto_mode=True, controlled_mode=True, rf_meter=True, antenna_ports=255,
        )
        station = ReferenceStation("n" * 32, {
            name: TunerSource(caps, lambda: self.snapshot) for name in reversed(devices)
        })
        status, payload = station.http("GET", "/shack/v1/devices",
                                       {**HTTP_HEADERS, "Shack-Request-Id": "r" * 32})
        self.assertEqual(status, 200)
        self.assertLessEqual(len(payload), MAX_PACKET)
        page = decode_json(payload, wire.Response()).catalog
        self.assertEqual([d.device_id for d in page.devices], devices[:MAX_CATALOG_PAGE])
        self.assertEqual(page.next_after_device_id, devices[MAX_CATALOG_PAGE - 1])
        status, payload = station.http("GET", "/shack/v1/devices?after=" + page.next_after_device_id,
                                       HTTP_HEADERS)
        self.assertEqual(status, 200)
        page = decode_json(payload, wire.Response()).catalog
        self.assertEqual([d.device_id for d in page.devices], devices[MAX_CATALOG_PAGE:])
        self.assertFalse(page.next_after_device_id)
        invalid_limit = MAX_CATALOG_PAGE + 1
        response = self.through_all(f"/shack/v1/devices?limit={invalid_limit}", self.request(
            list_devices=wire.ListDevicesRequest(limit=invalid_limit)
        ), 400)
        self.assertEqual(response.error.code, wire.Error.INVALID_REQUEST)

    def test_missing_and_unavailable_tuners(self):
        request = self.request(get_tuner_state=wire.GetTunerStateRequest(device_id="missing"))
        response = self.through_all("/shack/v1/devices/missing/state", request, 404)
        self.assertEqual(response.error.code, wire.Error.NOT_FOUND)
        self.snapshot = None
        response = self.through_all(STATE_PATH, self.state_request(), 503)
        self.assertEqual(response.error.code, wire.Error.UNAVAILABLE)

    def test_unknown_samples_do_not_become_zero(self):
        self.snapshot.ClearField("antenna")
        self.snapshot.ClearField("meter_age_ms")
        self.snapshot.meter.forward_watts = float("nan")
        self.snapshot.meter.swr = float("inf")
        state = self.through_all(STATE_PATH, self.state_request()).tuner_state
        self.assertFalse(state.HasField("antenna"))
        self.assertFalse(state.HasField("meter_age_ms"))
        self.assertFalse(state.meter.HasField("forward_watts"))
        self.assertFalse(state.meter.HasField("swr"))
        self.assertTrue(state.meter.HasField("reflected_watts"))

    def test_incompatible_version_and_wrong_node(self):
        request = self.state_request()
        request.version.minor = 1
        response = decode_datagram(self.station.udp(encode_datagram(request)), wire.Response())
        self.assertEqual(response.error.code, wire.Error.UNSUPPORTED_VERSION)
        self.assertEqual(response.version, version())
        status, payload = self.station.http("GET", STATE_PATH, {**HTTP_HEADERS, "Shack-Version": "1.1"})
        self.assertEqual(status, 400)
        self.assertEqual(decode_json(payload, wire.Response()), response)
        request.node_id = "wrong"
        response = decode_datagram(self.station.udp(encode_datagram(request)), wire.Response())
        self.assertEqual(response.error.code, wire.Error.NOT_FOUND)

    def test_mqtt_requires_correlated_topic_and_live_delivery(self):
        payload = encode_json(self.state_request())
        for topic, retained in [(MQTT_REQUEST, True), (MQTT_REPLY, False),
                                (MQTT_REQUEST.replace(REQUEST, "req-2"), False)]:
            with self.subTest(topic=topic, retained=retained), self.assertRaises(BindingError):
                self.station.mqtt(topic, payload, retained=retained)
        request = self.state_request()
        request.node_id = "another-station"
        with self.assertRaises(BindingError):
            self.station.mqtt(MQTT_REQUEST, encode_json(request), retained=False)

    def test_invalid_shapes_and_limits(self):
        with self.assertRaises(BindingError):
            self.station.udp(encode_datagram(self.state_request())[:-1])
        with self.assertRaises(BindingError):
            self.station.udp(b"x" * (MAX_PACKET + 1))
        with self.assertRaises(BindingError):
            self.station.mqtt(MQTT_REQUEST, b'{"unknown":true}', retained=False)
        with self.assertRaises(BindingError):
            self.station.http("POST", STATE_PATH, HTTP_HEADERS)
        with self.assertRaises(BindingError):
            self.station.http("GET", STATE_PATH, {**HTTP_HEADERS, "Shack-Request-Id": "+"})
        with self.assertRaises(BindingError):
            self.station.http("GET", "/shack/v1/devices?limit=1&limit=2", HTTP_HEADERS)
        request = self.request()
        response = decode_datagram(self.station.udp(encode_datagram(request)), wire.Response())
        self.assertEqual(response.error.code, wire.Error.INVALID_REQUEST)

    def test_usb_fragmentation_and_resynchronization(self):
        reader = UsbFramer()
        self.assertEqual(reader.feed(frame_usb(b"{}") * 2), [b"{}", b"{}"])
        self.assertEqual(reader.feed(b"@SHACK 3\n{"), [])
        reader.reset()
        for header in [b"@SHACK 0\n", b"@SHACK 1025\n", b"@SHACK 99999", b"bad\n"]:
            with self.subTest(header=header), self.assertRaises(BindingError):
                reader.feed(header)
        self.assertEqual(reader.feed(frame_usb(b"{}")), [b"{}"])

    def test_tuner_state_wire_vector(self):
        golden = json.loads(Path(__file__).with_name("tuner_state_vector.json").read_text())
        request = self.state_request()
        self.assertEqual(json.loads(encode_json(request)), golden["request"])
        self.assertEqual(encode_datagram(request).hex(), golden["requestHex"])
        binary = self.station.udp(bytes.fromhex(golden["requestHex"]))
        self.assertEqual(binary.hex(), golden["responseHex"])
        self.assertEqual(json.loads(encode_json(decode_datagram(binary, wire.Response()))),
                         golden["response"])
