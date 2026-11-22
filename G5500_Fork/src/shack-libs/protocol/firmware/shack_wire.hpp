// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "wire.pb.h"
#include "bindings.hpp"
#include <string>
#include <string_view>

namespace shack {
struct Station {
    std::string_view node;
    std::string_view device;
    const shack_v1_TunerCapabilities *tuner = nullptr;
    bool (*read)(shack_v1_TunerState &) = nullptr;
};

using Request = shack_v1_Request;
using Response = shack_v1_Response;
bool validId(std::string_view value);
bool readRequest(std::string_view bytes, Request &request);
bool writeResponse(const Response &response, std::string &bytes);
bool dispatch(const Station &station, const Request &request, Response &response);
bool http(const Station &station, std::string_view target, std::string_view version,
          std::string_view id, int &status, std::string &body);
bool udp(const Station &station, std::string_view packet, std::string &reply);
bool usb(const Station &station, std::string_view payload, std::string &body);
std::string mqttSubscription(std::string_view node);
bool mqttRequestTopic(std::string_view node, std::string_view topic);
bool mqtt(const Station &station, std::string_view topic, std::string_view packet,
          std::string &replyTopic, std::string &body);
bool matchesState(std::string_view json, std::string_view requestId, std::string_view deviceId);
std::string tunerPath(std::string_view deviceId);

class UsbFrames {
  public:
    bool active() const { return active_; }
    void expire(uint32_t now);
    // The caller offers the initial @ only outside its legacy command frame.
    bool feed(char c, uint32_t now);
    std::string_view payload() const { return {payload_, size_}; }
  private:
    char header_[16]{}, payload_[MaxPacket];
    size_t used_ = 0, length_ = 0, size_ = 0;
    uint32_t last_ = 0;
    bool active_ = false, invalid_ = false;
    void reset();
};
}
