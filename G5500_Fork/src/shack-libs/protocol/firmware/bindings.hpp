// Generated from protocol/bindings.json. Do not edit.
#pragma once
#include <cstddef>
#include <cstdint>
namespace shack {
inline constexpr unsigned Major = 1;
inline constexpr unsigned Minor = 0;
inline constexpr unsigned MaxPacket = 1024;
inline constexpr unsigned MaxId = 32;
inline constexpr unsigned MaxTarget = 128;
inline constexpr unsigned MaxPage = 3;
inline constexpr unsigned UdpPort = 4536;
inline constexpr char HelloPath[] = "/shack/v1/hello";
inline constexpr char CatalogPath[] = "/shack/v1/devices";
inline constexpr char StatePath[] = "/shack/v1/devices/{device_id}/state";
inline constexpr char VersionHeader[] = "Shack-Version";
inline constexpr char IdHeader[] = "Shack-Request-Id";
inline constexpr char UsbPrefix[] = "@SHACK ";
inline constexpr char MqttRequest[] = "shack/v1/{node_id}/read/{request_id}/request";
inline constexpr char MqttReply[] = "shack/v1/{node_id}/read/{request_id}/reply";
}
