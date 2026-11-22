// SPDX-License-Identifier: GPL-3.0-or-later
#include "shack_wire.hpp"
#include "nanopb/pb_decode.h"
#include "nanopb/pb_encode.h"
#ifdef ARDUINO
#include "arduinojson/ArduinoJson.h"
#else
#include <ArduinoJson.h>
#endif
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace shack {
namespace {
using View = std::string_view;
using Error = shack_v1_Error_Code;
constexpr View DeviceField = "{device_id}";
constexpr View NodeField = "{node_id}";
constexpr View RequestField = "{request_id}";
constexpr View StateRoute = StatePath;
const char *const Modes[] = {"TUNER_MODE_UNKNOWN", "TUNER_MODE_MANUAL", "TUNER_MODE_AUTO",
                             "TUNER_MODE_CONTROLLED", "TUNER_MODE_BYPASS"};
const char *const Errors[] = {"CODE_UNSPECIFIED", "INVALID_REQUEST", "UNSUPPORTED_VERSION",
                              "NOT_FOUND", "UNAVAILABLE"};

template<size_t N> bool copy(char (&target)[N], View value) {
    if (value.size() >= N)
        return false;
    if (!value.empty())
        memcpy(target, value.data(), value.size());
    target[value.size()] = 0;
    return true;
}
bool starts(View value, View prefix) { return value.substr(0, prefix.size()) == prefix; }
bool ends(View value, View suffix) {
    return value.size() >= suffix.size() && value.substr(value.size() - suffix.size()) == suffix;
}
bool unsignedNumber(View text, uint32_t &value) {
    if (text.empty())
        return false;
    value = 0;
    for (char c : text) {
        if (c < '0' || c > '9' || value > (std::numeric_limits<uint32_t>::max() - (c - '0')) / 10)
            return false;
        value = value * 10 + (c - '0');
    }
    return true;
}
bool jsonVersion(JsonObjectConst obj, shack_v1_Version &v) {
    if (!obj["major"].is<uint32_t>() ||
        (!obj["minor"].isUnbound() && !obj["minor"].is<uint32_t>()))
        return false;
    v = {obj["major"].as<uint32_t>(), obj["minor"] | 0u};
    return true;
}
bool jsonObject(View input, JsonDocument &doc) {
    return !input.empty() && input.size() <= MaxPacket &&
           !deserializeJson(doc, input.data(), input.size(), DeserializationOption::NestingLimit(8)) &&
           doc.is<JsonObject>();
}
bool jsonField(JsonObjectConst obj, const char *key, View &value) {
    if (!obj[key].is<JsonString>())
        return false;
    const auto text = obj[key].as<JsonString>();
    value = {text.c_str(), text.size()};
    return value.find('\0') == View::npos;
}
void fail(Response &response, Error error) {
    response.which_reply = shack_v1_Response_error_tag;
    response.reply.error.code = error;
}
std::string substitute(View pattern, View field, View value) {
    const auto where = pattern.find(field);
    std::string result(pattern.substr(0, where));
    result.append(value);
    result.append(pattern.substr(where + field.size()));
    return result;
}
std::string topicFor(View pattern, View node, View id) {
    auto result = substitute(pattern, NodeField, node);
    return substitute(result, RequestField, id);
}
bool catalogQuery(View query, shack_v1_ListDevicesRequest &request) {
    bool limit = false, after = false;
    while (!query.empty()) {
        const auto end = query.find('&');
        const auto item = query.substr(0, end);
        if (starts(item, "limit=") && !limit)
            limit = unsignedNumber(item.substr(6), request.limit);
        else if (starts(item, "after=") && !after)
            after = copy(request.after_device_id, item.substr(6));
        else
            return false;
        if (!(starts(item, "limit=") ? limit : after))
            return false;
        if (end == View::npos)
            return true;
        query.remove_prefix(end + 1);
        if (query.empty())
            return false;
    }
    return true;
}
bool route(const Station &station, View target, View id, shack_v1_Version version, Request &r) {
    if (target.size() > MaxTarget || !validId(id) || !copy(r.node_id, station.node))
        return false;
    copy(r.request_id, id);
    r.has_version = true;
    r.version = version;
    if (target == HelloPath) {
        r.which_call = shack_v1_Request_hello_tag;
        return true;
    }
    const auto separator = target.find('?');
    if (target.substr(0, separator) == CatalogPath) {
        r.which_call = shack_v1_Request_list_devices_tag;
        return separator == View::npos || catalogQuery(target.substr(separator + 1), r.call.list_devices);
    }
    const auto at = StateRoute.find(DeviceField);
    const auto prefix = StateRoute.substr(0, at);
    const auto suffix = StateRoute.substr(at + DeviceField.size());
    if (!starts(target, prefix) || !ends(target, suffix) || target.size() < prefix.size() + suffix.size())
        return false;
    const auto device = target.substr(prefix.size(), target.size() - prefix.size() - suffix.size());
    r.which_call = shack_v1_Request_get_tuner_state_tag;
    return validId(device) && copy(r.call.get_tuner_state.device_id, device);
}
void writeCaps(JsonObject out, const shack_v1_TunerCapabilities &c) {
    if (c.tune) out["tune"] = true;
    if (c.memory_tune) out["memoryTune"] = true;
    if (c.full_tune) out["fullTune"] = true;
    if (c.toggle_bypass) out["toggleBypass"] = true;
    if (c.manual_mode) out["manualMode"] = true;
    if (c.auto_mode) out["autoMode"] = true;
    if (c.controlled_mode) out["controlledMode"] = true;
    if (c.rf_meter) out["rfMeter"] = true;
    if (c.antenna_ports) out["antennaPorts"] = c.antenna_ports;
}
}

bool validId(View value) {
    if (value.empty() || value.size() > MaxId)
        return false;
    auto alnum = [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    };
    return alnum(value.front()) && std::all_of(value.begin() + 1, value.end(), [&](char c) {
        return alnum(c) || c == '-' || c == '_' || c == '.';
    });
}

bool readRequest(View bytes, Request &request) {
    request = {};
    JsonDocument d;
    if (!jsonObject(bytes, d))
        return false;
    const auto root = d.as<JsonObjectConst>();
    View id, node;
    if (!jsonField(root, "requestId", id) || !jsonField(root, "nodeId", node) ||
        !validId(id) || !validId(node) || !jsonVersion(root["version"].as<JsonObjectConst>(), request.version))
        return false;
    const auto hello = root["hello"];
    const auto list = root["listDevices"];
    const auto state = root["getTunerState"];
    const unsigned calls = unsigned(!hello.isUnbound()) + unsigned(!list.isUnbound()) +
                           unsigned(!state.isUnbound());
    if (calls > 1 || root.size() != 3 + calls)
        return false;
    copy(request.request_id, id);
    copy(request.node_id, node);
    request.has_version = true;
    if (!hello.isUnbound()) {
        if (!hello.is<JsonObjectConst>() || hello.size())
            return false;
        request.which_call = shack_v1_Request_hello_tag;
    } else if (!list.isUnbound()) {
        if (!list.is<JsonObjectConst>())
            return false;
        auto q = list.as<JsonObjectConst>();
        const auto n = q["limit"];
        View after;
        unsigned fields = 0;
        if (!n.isUnbound()) {
            if (!n.is<uint32_t>()) return false;
            request.call.list_devices.limit = n.as<uint32_t>();
            ++fields;
        }
        if (!q["afterDeviceId"].isUnbound()) {
            if (!jsonField(q, "afterDeviceId", after) ||
                !copy(request.call.list_devices.after_device_id, after)) return false;
            ++fields;
        }
        if (q.size() != fields)
            return false;
        request.which_call = shack_v1_Request_list_devices_tag;
    } else if (!state.isUnbound()) {
        auto q = state.as<JsonObjectConst>();
        View device;
        if (q.size() != 1 || !jsonField(q, "deviceId", device) ||
            !copy(request.call.get_tuner_state.device_id, device))
            return false;
        request.which_call = shack_v1_Request_get_tuner_state_tag;
    }
    return true;
}

bool dispatch(const Station &station, const Request &r, Response &out) {
    out = {};
    if (!validId(r.request_id) || !validId(r.node_id) || !validId(station.node))
        return false;
    copy(out.request_id, r.request_id);
    copy(out.node_id, station.node);
    out.has_version = true;
    out.version = {Major, Minor};
    if (r.node_id != station.node)
        fail(out, shack_v1_Error_Code_NOT_FOUND);
    else if (!r.has_version || r.version.major != Major || r.version.minor != Minor)
        fail(out, shack_v1_Error_Code_UNSUPPORTED_VERSION);
    else if (r.which_call == shack_v1_Request_hello_tag) {
        out.which_reply = shack_v1_Response_hello_tag;
        out.reply.hello = {MaxPacket, MaxPage};
    } else if (r.which_call == shack_v1_Request_list_devices_tag) {
        const auto &q = r.call.list_devices;
        if (q.limit > MaxPage || (q.after_device_id[0] && !validId(q.after_device_id))) {
            fail(out, shack_v1_Error_Code_INVALID_REQUEST);
            return true;
        }
        out.which_reply = shack_v1_Response_catalog_tag;
        auto &catalog = out.reply.catalog;
        if (!station.device.empty() && station.device > View(q.after_device_id)) {
            auto &device = catalog.devices[catalog.devices_count++];
            if (!validId(station.device) || !copy(device.device_id, station.device))
                return false;
            if (station.tuner) {
                device.which_capabilities = shack_v1_Device_tuner_tag;
                device.capabilities.tuner = *station.tuner;
            }
        }
    } else if (r.which_call == shack_v1_Request_get_tuner_state_tag) {
        const View device = r.call.get_tuner_state.device_id;
        if (!validId(device))
            fail(out, shack_v1_Error_Code_INVALID_REQUEST);
        else if (device != station.device || !station.tuner)
            fail(out, shack_v1_Error_Code_NOT_FOUND);
        else if (!station.read || !station.read(out.reply.tuner_state))
            fail(out, shack_v1_Error_Code_UNAVAILABLE);
        else {
            auto &s = out.reply.tuner_state;
            copy(s.device_id, device);
            s.has_antenna = s.has_antenna && s.antenna < station.tuner->antenna_ports;
            s.meter.has_forward_watts &= std::isfinite(s.meter.forward_watts);
            s.meter.has_reflected_watts &= std::isfinite(s.meter.reflected_watts);
            s.meter.has_swr &= std::isfinite(s.meter.swr);
            s.has_meter &= s.meter.has_forward_watts || s.meter.has_reflected_watts || s.meter.has_swr;
            out.which_reply = shack_v1_Response_tuner_state_tag;
        }
    } else
        fail(out, shack_v1_Error_Code_INVALID_REQUEST);
    return true;
}

bool writeResponse(const Response &r, std::string &bytes) {
    bytes.clear();
    JsonDocument d;
    d["version"]["major"] = r.version.major;
    if (r.version.minor) d["version"]["minor"] = r.version.minor;
    d["requestId"] = r.request_id;
    d["nodeId"] = r.node_id;
    switch (r.which_reply) {
    case shack_v1_Response_hello_tag:
        d["hello"]["maxMessageBytes"] = r.reply.hello.max_message_bytes;
        d["hello"]["maxCatalogPage"] = r.reply.hello.max_catalog_page;
        break;
    case shack_v1_Response_catalog_tag: {
        auto catalog = d["catalog"].to<JsonObject>();
        if (r.reply.catalog.devices_count) {
            auto devices = catalog["devices"].to<JsonArray>();
            for (unsigned i = 0; i < r.reply.catalog.devices_count; ++i) {
                const auto &device = r.reply.catalog.devices[i];
                auto item = devices.add<JsonObject>();
                item["deviceId"] = device.device_id;
                if (device.which_capabilities == shack_v1_Device_tuner_tag)
                    writeCaps(item["tuner"].to<JsonObject>(), device.capabilities.tuner);
            }
        }
        break;
    }
    case shack_v1_Response_tuner_state_tag: {
        const auto &s = r.reply.tuner_state;
        auto state = d["tunerState"].to<JsonObject>();
        state["deviceId"] = s.device_id;
        if (s.connected) state["connected"] = true;
        if (s.busy) state["busy"] = true;
        if (s.sequence) state["sequence"] = s.sequence;
        if (unsigned(s.mode) < sizeof(Modes) / sizeof(*Modes) && s.mode)
            state["mode"] = Modes[s.mode];
        if (s.has_antenna) state["antenna"] = s.antenna;
        if (s.has_meter_age_ms) state["meterAgeMs"] = s.meter_age_ms;
        if (s.has_meter) {
            auto m = state["meter"].to<JsonObject>();
            if (s.meter.has_forward_watts) m["forwardWatts"] = s.meter.forward_watts;
            if (s.meter.has_reflected_watts) m["reflectedWatts"] = s.meter.reflected_watts;
            if (s.meter.has_swr) m["swr"] = s.meter.swr;
        }
        break;
    }
    case shack_v1_Response_error_tag:
        if (unsigned(r.reply.error.code) >= sizeof(Errors) / sizeof(*Errors))
            return false;
        d["error"]["code"] = Errors[r.reply.error.code];
        break;
    default: return false;
    }
    return !d.overflowed() && measureJson(d) <= MaxPacket && serializeJson(d, bytes) != 0;
}

bool http(const Station &station, View target, View text, View id, int &status, std::string &body) {
    const auto dot = text.find('.');
    shack_v1_Version v{};
    Request request{};
    Response response{};
    if (dot == View::npos || !unsignedNumber(text.substr(0, dot), v.major) ||
        !unsignedNumber(text.substr(dot + 1), v.minor) || !route(station, target, id, v, request) ||
        !dispatch(station, request, response) || !writeResponse(response, body))
        return false;
    status = 200;
    if (response.which_reply == shack_v1_Response_error_tag)
        status = response.reply.error.code == shack_v1_Error_Code_NOT_FOUND ? 404 :
                 response.reply.error.code == shack_v1_Error_Code_UNAVAILABLE ? 503 : 400;
    return true;
}

bool udp(const Station &station, View packet, std::string &reply) {
    if (packet.empty() || packet.size() > MaxPacket)
        return false;
    Request request{};
    Response response{};
    auto input = pb_istream_from_buffer(reinterpret_cast<const pb_byte_t *>(packet.data()), packet.size());
    if (!pb_decode(&input, shack_v1_Request_fields, &request) || !dispatch(station, request, response))
        return false;
    pb_byte_t bytes[shack_v1_Response_size];
    auto output = pb_ostream_from_buffer(bytes, sizeof bytes);
    if (!pb_encode(&output, shack_v1_Response_fields, &response))
        return false;
    reply.assign(reinterpret_cast<const char *>(bytes), output.bytes_written);
    return true;
}

bool usb(const Station &station, View payload, std::string &body) {
    JsonDocument d;
    View id, target, method;
    shack_v1_Version version{};
    Request request{};
    Response response{};
    if (!jsonObject(payload, d))
        return false;
    const auto obj = d.as<JsonObjectConst>();
    return obj.size() == 4 && jsonField(obj, "requestId", id) && jsonField(obj, "target", target) &&
           jsonField(obj, "method", method) && method == "GET" &&
           jsonVersion(obj["version"].as<JsonObjectConst>(), version) &&
           route(station, target, id, version, request) && dispatch(station, request, response) &&
           writeResponse(response, body);
}

std::string mqttSubscription(View node) { return topicFor(MqttRequest, node, "+"); }
bool mqttRequestTopic(View node, View topic) {
    const auto pattern = mqttSubscription(node);
    const auto split = pattern.find('+');
    const auto prefix = View(pattern).substr(0, split);
    const auto suffix = View(pattern).substr(split + 1);
    return starts(topic, prefix) && ends(topic, suffix) && topic.size() >= prefix.size() + suffix.size() &&
           validId(topic.substr(prefix.size(), topic.size() - prefix.size() - suffix.size()));
}
bool mqtt(const Station &station, View topic, View packet, std::string &replyTopic, std::string &body) {
    Request request{};
    Response response{};
    if (!readRequest(packet, request) || request.node_id != station.node ||
        topicFor(MqttRequest, station.node, request.request_id) != topic ||
        !dispatch(station, request, response) || !writeResponse(response, body))
        return false;
    replyTopic = topicFor(MqttReply, station.node, request.request_id);
    return true;
}
std::string tunerPath(View deviceId) {
    return validId(deviceId) ? substitute(StatePath, DeviceField, deviceId) : std::string{};
}
bool matchesState(View bytes, View requestId, View deviceId) {
    JsonDocument d;
    if (!jsonObject(bytes, d))
        return false;
    const auto obj = d.as<JsonObjectConst>();
    shack_v1_Version v{};
    View id, node, device;
    return obj.size() == 4 && jsonVersion(obj["version"].as<JsonObjectConst>(), v) &&
           v.major == Major && v.minor == Minor && jsonField(obj, "requestId", id) && requestId == id &&
           jsonField(obj, "nodeId", node) && validId(node) &&
           jsonField(obj["tunerState"].as<JsonObjectConst>(), "deviceId", device) && device == deviceId;
}
void UsbFrames::reset() {
    used_ = length_ = size_ = 0;
    invalid_ = active_ = false;
}
void UsbFrames::expire(uint32_t now) {
    if (active_ && now - last_ >= 2000)
        reset();
}
bool UsbFrames::feed(char c, uint32_t now) {
    if (!active_) {
        if (c != '@')
            return false;
        reset();
        active_ = true;
    }
    last_ = now;
    if (length_) {
        payload_[size_++] = c;
        if (size_ == length_) {
            active_ = false;
            return true;
        }
    } else if (c == '\n') {
        const View header(header_, used_);
        uint32_t n = 0;
        if (invalid_ || !starts(header, UsbPrefix) ||
            !unsignedNumber(header.substr(sizeof UsbPrefix - 1), n) || !n || n > MaxPacket) {
            reset();
        } else
            length_ = n;
    } else if (used_ < sizeof header_) {
        header_[used_++] = c;
    } else
        invalid_ = true;
    return false;
}
}
