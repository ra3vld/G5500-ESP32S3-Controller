#!/usr/bin/env python3
"""Generate the Python codec and bounded nanopb options from the contract."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = "shack_protocol"


def nanopb_options(bindings):
    limits = bindings["limits"]
    ids = (
        "Request.request_id", "Request.node_id", "UsbRequest.request_id",
        "ListDevicesRequest.after_device_id", "GetTunerStateRequest.device_id",
        "Device.device_id", "Catalog.next_after_device_id", "TunerState.device_id",
        "Response.request_id", "Response.node_id",
    )
    methods = [entry.split()[0] for name, entry in bindings["http"].items()
               if not name.endswith("_header")]
    options = {field: f"max_length:{limits['id_bytes']}" for field in ids}
    options["UsbRequest.target"] = f"max_length:{limits['http_target_bytes']}"
    options["UsbRequest.method"] = f"max_length:{max(map(len, methods))}"
    options["Catalog.devices"] = f"max_count:{limits['catalog_page']}"
    return "".join(f"shack.v1.{field} {options[field]}\n" for field in sorted(options)).encode()


def firmware_bindings(bindings):
    major, minor = (bindings["version"][name] for name in ("major", "minor"))
    http = bindings["http"]
    limits = bindings["limits"]
    numbers = {
        "Major": major, "Minor": minor, "MaxPacket": limits["message_bytes"],
        "MaxId": limits["id_bytes"], "MaxTarget": limits["http_target_bytes"],
        "MaxPage": limits["catalog_page"], "UdpPort": bindings["udp"]["port"],
    }
    strings = {
        "HelloPath": http["hello"].split(" ", 1)[1],
        "CatalogPath": http["list_devices"].split(" ", 1)[1],
        "StatePath": http["get_tuner_state"].split(" ", 1)[1],
        "VersionHeader": http["version_header"], "IdHeader": http["request_id_header"],
        "UsbPrefix": bindings["usb"]["prefix"],
        "MqttRequest": bindings["mqtt"]["request"], "MqttReply": bindings["mqtt"]["response"],
    }
    lines = ["// Generated from protocol/bindings.json. Do not edit.", "#pragma once",
             "#include <cstddef>", "#include <cstdint>", "namespace shack {"]
    lines.extend(f"inline constexpr unsigned {name} = {value};" for name, value in numbers.items())
    for name, value in strings.items():
        route = json.dumps(value.replace("{major}", str(major)))
        lines.append(f"inline constexpr char {name}[] = {route};")
    lines.append("}")
    return ("\n".join(lines) + "\n").encode()


def update(path, content, verify):
    if verify:
        if not path.is_file() or path.read_bytes() != content:
            raise SystemExit(f"{path.relative_to(ROOT)} is out of date")
    else:
        path.write_bytes(content)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--nanopb-plugin", type=Path)
    args = parser.parse_args()
    schema = ROOT / "schema"
    with tempfile.TemporaryDirectory() as directory:
        subprocess.run(
            [sys.executable, "-m", "grpc_tools.protoc", f"-I{schema}",
             f"--python_out={directory}", str(schema / PACKAGE / "wire.proto")],
            check=True,
        )
        generated = (Path(directory) / PACKAGE / "wire_pb2.py").read_bytes()
        bindings = json.loads((ROOT / "bindings.json").read_text())
        update(ROOT / PACKAGE / "wire_pb2.py", generated, args.check)
        update(schema / PACKAGE / "wire.options", nanopb_options(bindings), args.check)
        update(ROOT / "firmware" / "bindings.hpp", firmware_bindings(bindings), args.check)
        if args.nanopb_plugin:
            subprocess.run([
                sys.executable, "-m", "grpc_tools.protoc", f"-I{schema}",
                f"--plugin=protoc-gen-nanopb={args.nanopb_plugin.resolve()}",
                f"--nanopb_out=-I{schema}:{directory}", str(schema / PACKAGE / "wire.proto"),
            ], check=True)
            for name, old, new in (
                ("wire.pb.c", b'"shack_protocol/wire.pb.h"', b'"wire.pb.h"'),
                ("wire.pb.h", b'<pb.h>', b'"nanopb/pb.h"'),
            ):
                content = (Path(directory) / PACKAGE / name).read_bytes().replace(old, new).rstrip() + b"\n"
                update(ROOT / "firmware" / name, content, args.check)


if __name__ == "__main__":
    main()
