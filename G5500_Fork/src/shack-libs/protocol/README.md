# Shack Protocol v1.0

[Русская версия](README.ru.md)

The first implementation reads node information, a paginated device catalog and tuner state. The host reference takes a mapping of device IDs to capability descriptors and snapshot callbacks. It can be used in a simulator or in tests for future device adapters.

[The Protobuf schema](schema/shack_protocol/wire.proto) defines the shared types. [The bindings manifest](bindings.json) defines the version, transport bindings and shared limits. The generator produces the Python codec and bounded nanopb options from those files. Each response includes the station ID, request ID and supported version. ProtoJSON can omit zero-valued scalars such as minor version zero. Optional measurements, antenna indices and sample ages preserve the difference between zero and missing data. [ProtoJSON rules](https://protobuf.dev/programming-guides/json/) · [Nanopb options](https://jpa.kapsi.fi/nanopb/docs/reference.html)

| Transport | v1.0 binding |
|---|---|
| HTTP | Send `GET /shack/v1/hello`, `GET /shack/v1/devices` or `GET /shack/v1/devices/{device_id}/state` with `Shack-Version: 1.0` and `Shack-Request-Id`. The response uses ProtoJSON. Pass the optional `after` and `limit` query parameters to page the catalog. |
| USB | Send length-prefixed ProtoJSON `UsbRequest` with the same method and target as HTTP. Each frame is `@SHACK <decimal byte length>\n<JSON bytes>`. The response uses the same frame format and the HTTP response body semantics. Send one frame per request. Reset the input framer after disconnect or an incomplete-frame timeout. |
| UDP | Encode one binary Protobuf `Request` per datagram. A successful decode and dispatch produces one binary `Response` to send to the same peer. |
| MQTT | Publish ProtoJSON `Request` to `shack/v1/{node_id}/read/{request_id}/request`. Subscribe to the corresponding `…/reply` topic before publishing. Use QoS 1 and disable retain for both. The node and request IDs must match the topic. |

The message ceiling is 1024 bytes excluding the USB frame header. Catalog pages contain up to three records in lexical ID order. Each page can include `nextAfterDeviceId` as the cursor for the next query. IDs are case-sensitive ASCII, up to 32 bytes. They start with a letter or digit and may contain `.`, `_` and `-`. The HTTP request target has a 128-byte limit. A valid envelope with an unsupported version returns `UNSUPPORTED_VERSION` and the current version. Structural errors raise `BindingError`, which a listener can map to HTTP 400 or discard at the message transport boundary.

## Verification

```sh
python3 -m venv /tmp/shack-protocol-venv
. /tmp/shack-protocol-venv/bin/activate
python -m pip install -r protocol/requirements.txt
python protocol/tools/generate.py --check
PYTHONPATH=protocol python -m unittest discover -s protocol/tests -v
```

Run the generator without `--check` to update the checked-in files. [The test vector](tests/tuner_state_vector.json) records the same request and response in ProtoJSON and binary Protobuf. The tests exercise the transport bindings in memory. HTTP/USB listeners, authenticated UDP transport, MQTT client registration and mutation handlers belong to firmware integration.
