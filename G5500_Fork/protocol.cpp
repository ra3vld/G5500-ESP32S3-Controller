// G5500 Fork; original by UA1CFM. GPL-3.0.
// Bounded, fully validated XML subset for N1MM/PST; no DTD or entities expansion.
#include "protocol.hpp"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <array>
#include "json_writer.hpp"
#include <algorithm>
namespace forkrot {

namespace {
std::string_view trimView(std::string_view s) {
  size_t a = 0, b = s.size();
  while (a < b && (std::isspace((unsigned char)s[a]) || s[a] == 0)) {
    ++a;
  }
  while (b > a && (std::isspace((unsigned char)s[b - 1]) || s[b - 1] == 0)) {
    --b;
  }
  return s.substr(a, b - a);
}
struct Node {
  std::string_view name, text;
  Node *next = nullptr;
  struct Children {
    Node *first = nullptr;
    bool empty() const { return !first; }
    struct Iterator {
      Node *node;
      const Node &operator*() const { return *node; }
      Iterator &operator++() { node = node->next; return *this; }
      bool operator!=(Iterator other) const { return node != other.node; }
    };
    Iterator begin() const { return {first}; }
    Iterator end() const { return {nullptr}; }
  } children;
};
class Xml {
  std::string_view input_;
  size_t position_ = 0, used_ = 0;
  std::array<Node, 39> nodes_{}; // root + 39 children; views point into the UDP packet
  void ws() {
    while (position_ < input_.size() && std::isspace((unsigned char)input_[position_])) ++position_;
  }
  std::string_view name() {
    size_t start = position_;
    while (position_ < input_.size() &&
           (std::isalnum((unsigned char)input_[position_]) || input_[position_] == '_' ||
            input_[position_] == '+' || input_[position_] == '-')) ++position_;
    return input_.substr(start, position_ - start);
  }
  bool node(Node &n, unsigned depth) {
    if (depth > 3 || position_ >= input_.size() || input_[position_++] != '<') return false;
    n.name = name();
    if (n.name.empty() || n.name.size() > 32 || position_ >= input_.size()) return false;
    if (input_.compare(position_, 2, "/>") == 0) { position_ += 2; return true; }
    if (input_[position_++] != '>') return false;
    size_t textStart = position_;
    Node **tail = &n.children.first;
    for (;;) {
      if (position_ >= input_.size()) return false;
      if (input_.compare(position_, 2, "</") == 0) {
        const auto text = trimView(input_.substr(textStart, position_ - textStart));
        if (!n.children.empty() && !text.empty()) return false;
        n.text = text;
        position_ += 2;
        if (name() != n.name || position_ >= input_.size() || input_[position_++] != '>') return false;
        return true;
      }
      if (input_[position_] == '<') {
        if (!trimView(input_.substr(textStart, position_ - textStart)).empty() || used_ == nodes_.size()) return false;
        Node &child = nodes_[used_++];
        if (!node(child, depth + 1)) return false;
        for (const auto &other : n.children) if (other.name == child.name) return false;
        *tail = &child;
        tail = &child.next;
        textStart = position_;
      } else ++position_;
    }
  }
public:
  explicit Xml(std::string_view input) : input_(input) {}
  bool parse(Node &root) {
    ws();
    if (input_.compare(position_, 5, "<?xml") == 0) {
      size_t end = input_.find("?>", position_);
      if (end == input_.npos) return false;
      position_ = end + 2;
      ws();
    }
    if (!node(root, 0)) return false;
    ws();
    return position_ == input_.size();
  }
};
const Node *field(const Node &n, std::string_view key) {
  for (const auto &c : n.children) if (c.name == key) return &c;
  return nullptr;
}
bool flag(const Node *n, bool &b) {
  if (!n) {
    b = false;
    return true;
  }
  if (!n->children.empty() || (n->text != "0" && n->text != "1")) {
    return false;
  }
  b = n->text == "1";
  return true;
}
struct Words : FixedVector<std::string_view, 34> { bool valid = true; };
Words words(std::string_view text) {
  Words out;
  while (!text.empty()) {
    while (!text.empty() && std::isspace((unsigned char)text.front())) text.remove_prefix(1);
    if (text.empty()) break;
    size_t end = 0;
    while (end < text.size() && !std::isspace((unsigned char)text[end])) ++end;
    if (!out.push_back(text.substr(0, end))) { out.valid = false; break; }
    text.remove_prefix(end);
  }
  return out;
}
} // namespace
Packet Protocol::datagram(std::string_view input, uint32_t now) {
  Packet packet;
  auto text = trimView(input);
  if (text.size() > 4096 || text.find("<!") != std::string::npos ||
      text.find('&') != std::string::npos) {
    return packet;
  }
  Node root;
  Xml xml(text);
  if (!xml.parse(root)) {
    return packet;
  }
  if (root.name == "N1MMRotor") {
    if (!root.text.empty()) {
      return packet;
    }
    for (const auto &n : root.children) {
      if (n.name != "stop" && n.name != "rotor" && n.name != "goazi" && n.name != "offset" &&
          n.name != "bidirectional" && n.name != "freqband" && n.name != "app") {
        return packet;
      }
      if (n.name != "stop" && !n.children.empty()) {
        return packet;
      }
    }
    const Node *stopNode = field(root, "stop");
    if (stopNode) {
      if (!stopNode->text.empty() || field(root, "goazi")) {
        return packet;
      }
      for (const auto &n : stopNode->children) {
        if ((n.name != "rotor" && n.name != "freqband") || !n.children.empty()) {
          return packet;
        }
      }
    }
    const Node *rotor = field(root, "rotor");
    if (!rotor && stopNode) {
      rotor = field(*stopNode, "rotor");
    }
    if (!rotor || !rotor->children.empty()) {
      return packet;
    }
    bool match = false;
    auto names = rotor->text;
    while (!names.empty()) {
      auto comma = names.find(',');
      auto name = trimView(names.substr(0, comma));
      if (name == rotorName || name == "*") match = true;
      if (comma == names.npos) break;
      names.remove_prefix(comma + 1);
    }
    if (!match) {
      packet.result = Result::Ignored;
      return packet;
    }
    packet.matched = true;
    if (stopNode) {
      float offset;
      bool bidirectional;
      if (auto offsetField = field(root, "offset")) {
        if (!number(offsetField->text, offset) || std::abs(offset) > 360) {
          return packet;
        }
      }
      if (!flag(field(root, "bidirectional"), bidirectional)) {
        return packet;
      }
    }
    if (stopNode) {
      controller_.stop();
      packet.result = Result::Ok;
      return packet;
    }
    auto az = field(root, "goazi");
    float value = 0, offset = 0;
    bool bidirectional = false;
    if (!az || !az->children.empty() || !number(az->text, value) || value < 0 || value > 360) {
      return packet;
    }
    auto offsetField = field(root, "offset");
    if (offsetField && (!offsetField->children.empty() || !number(offsetField->text, offset) ||
                        std::abs(offset) > 360)) {
      return packet;
    }
    if (!flag(field(root, "bidirectional"), bidirectional)) {
      return packet;
    }
    value =
        nearest(value + offset, controller_.axes[0].current, controller_.maximum, bidirectional);
    packet.result = controller_.target({value, 0, true, false}, true, now);
    return packet;
  }
  if (root.name != "PST") {
    return packet;
  }
  packet.matched = true;
  if (!root.children.empty() && !root.text.empty()) {
    return packet;
  }
  for (const auto &n : root.children) {
    if (!n.children.empty()) {
      return packet;
    }
  }
  bool stop = false;
  for (const char *tag : {"STOP", "STOPR", "LEFT", "RIGHT", "LP", "TRACK", "PARK"}) {
    bool b = false;
    auto n = field(root, tag);
    if (!flag(n, b)) {
      return packet;
    }
    if ((std::string_view(tag) == "STOP" || std::string_view(tag) == "STOPR") && b) {
      stop = true;
    }
  }
  auto az = field(root, "AZIMUTH"), el = field(root, "ELEVATION");
  float azimuth = 0, elevation = 0;
  if (az && (!number(az->text, azimuth) || azimuth < 0 || azimuth > controller_.maximum)) {
    return packet;
  }
  if (el && (!number(el->text, elevation) || elevation < 0 || elevation > 180 ||
             (controller_.model == Model::G800DXA && elevation != 0))) {
    return packet;
  }
  for (const auto &n : root.children) {
    if (n.name != "AZIMUTH" && n.name != "ELEVATION" && n.name != "STOP" && n.name != "STOPR" &&
        n.name != "LEFT" && n.name != "RIGHT" && n.name != "LP" && n.name != "TRACK" &&
        n.name != "PARK" && n.name != "CALL" && n.name != "FREQUENCY" && n.name != "RADIO" &&
        n.name != "AZ" && n.name != "TGA") {
      return packet;
    }
  }
  if (stop) {
    controller_.stop();
    packet.result = Result::Ok;
    return packet;
  }
  auto query = root.text;
  if (auto n = field(root, "AZ")) {
    if (n->text != "?") {
      return packet;
    }
    query = "AZ?";
  }
  if (auto n = field(root, "TGA")) {
    if (n->text != "?") {
      return packet;
    }
    query = "TGA?";
  }
  if (!query.empty()) {
    if (query != "AZ?" && query != "TGA?") {
      return packet;
    }
    float value = controller_.axes[0].current;
    if (query == "TGA?") {
      value = controller_.pendingValid && controller_.pending.hasAz ? controller_.pending.az
                                                                    : controller_.axes[0].target;
    }
    char b[40];
    JsonWriter reply(b, sizeof b);
    reply << (query == "AZ?" ? "AZ:" : "TGA:");
    reply.decimal(heading(value), 1) << '\r';
    packet.reply.assign(reply.data(), reply.size());
    packet.result = Result::Ok;
    return packet;
  }
  bool left = false, right = false;
  flag(field(root, "LEFT"), left);
  flag(field(root, "RIGHT"), right);
  if (left && right) {
    return packet;
  }
  bool park = false;
  flag(field(root, "PARK"), park);
  if (park) {
    if (az || el || left || right || field(root, "LP") ||
        !controller_.valid(controller_.presets[0].target)) {
      return packet;
    }
    // UR5EQF's Park button uses preset 1 as the configurable parking position.
    // It remains an external numerical target: manual confirmation and preset priority apply.
    packet.result = controller_.target(controller_.presets[0].target, true, now);
    return packet;
  }
  // Preset priority also prevents ignored packets altering cached LP/base state.
  if (controller_.presetActive && (az || el || left || right || field(root, "LP"))) {
    packet.result = Result::Ignored;
    return packet;
  }
  if (left || right) {
    packet.result = controller_.jog(0, right ? 1 : -1, true, now);
    return packet;
  }
  bool lp = longPath_;
  if (auto n = field(root, "LP")) {
    flag(n, lp);
  }
  bool changed = lp != longPath_;
  if (az) {
    pstBaseAzimuth_ = azimuth;
    pstBaseValid_ = true;
  }
  longPath_ = lp;
  if (az || (changed && pstBaseValid_)) {
    float value = longPath_ ? heading(pstBaseAzimuth_ + 180) : pstBaseAzimuth_;
    value = nearest(value, controller_.axes[0].current, controller_.maximum);
    packet.result = controller_.target({value, elevation, true, el != nullptr}, true, now);
  } else if (el) {
    packet.result = controller_.target({0, elevation, false, true}, true, now);
  } else {
    packet.result = Result::Ok;
  }
  return packet;
}
std::string Protocol::positionReport(float mechanicalAz) const {
  // Real N1MM Logger+ decodes the heading in tenths of a degree.
  return rotorName + "@" + std::to_string(int(std::lround(heading(mechanicalAz) * 10)) % 3600);
}
Packet Protocol::line(std::string_view input, uint32_t now, bool tcp) {
  if (loadedStopEpoch_ != controller_.stopEpoch) {
    loadedSequence_.clear();
    loadedSequenceStarted_ = false;
    loadedStopEpoch_ = controller_.stopEpoch;
  }
  Packet packet;
  auto text = trimView(input);
  if (text.empty() || text.size() > 512) {
    return packet;
  }
  auto v = words(text);
  if (!v.valid || v.empty()) return packet;
  std::string_view cmd = v[0];
  char upper[16];
  const size_t commandLength = std::min(cmd.size(), sizeof upper);
  for (size_t i = 0; i < commandLength; ++i) upper[i] = std::toupper((unsigned char)cmd[i]);
  std::string_view u(upper, commandLength);
  if (u == "SET_POS" || u == "\\SET_POS" || u == "GET_POS" || u == "\\GET_POS" || u == "STOP" ||
      u == "\\STOP") {
    for (size_t i = 0; i < commandLength; ++i) upper[i] = std::tolower((unsigned char)upper[i]);
    cmd = {upper, commandLength};
  }
  bool ham = (cmd == "P" && v.size() > 1) || cmd == "p" || cmd == "\\set_pos" || cmd == "set_pos" ||
             cmd == "\\get_pos" || cmd == "get_pos" || cmd == "\\stop" || cmd == "stop" ||
             (tcp && u == "S");
  packet.matched = true;
  packet.result = Result::Ok;
  char b[100];
  if (cmd == "p" || cmd == "\\get_pos" || cmd == "get_pos") {
    JsonWriter reply(b, sizeof b);
    reply.decimal(controller_.axes[0].current, 6) << '\n';
    reply.decimal(controller_.axes[1].current, 6) << '\n';
    packet.reply.assign(reply.data(), reply.size());
    return packet;
  }
  if (u == "C" || u == "C2" || u == "B") {
    if (u == "C") {
      std::snprintf(b, sizeof b, "AZ=%03d\r\n", int(std::lround(controller_.axes[0].current)));
    } else if (u == "B") {
      std::snprintf(b, sizeof b, "EL=%03d\r\n", int(std::lround(controller_.axes[1].current)));
    } else {
      std::snprintf(b, sizeof b, "AZ=%03d EL=%03d\r\n",
                    int(std::lround(controller_.axes[0].current)),
                    int(std::lround(controller_.axes[1].current)));
    }
    packet.reply = b;
    return packet;
  }
  if (u == "S" || cmd == "\\stop" || cmd == "stop") {
    controller_.stop();
  } else if (u == "A" || u == "E") {
    controller_.stop(u == "A" ? 0 : 1);
  } else if (u == "L" || u == "R" || u == "U" || u == "D") {
    packet.result =
        controller_.jog(u == "U" || u == "D" ? 1 : 0, u == "R" || u == "U" ? 1 : -1, true, now);
  } else if (u == "X1" || u == "X2" || u == "X3" || u == "X4") {
    controller_.speedPercent = (u[1] - '0') * 25;
  } else if (cmd == "P" || cmd == "\\set_pos" || cmd == "set_pos") {
    float az = 0, el = 0;
    if (v.size() != 3 || !number(v[1], az) || !number(v[2], el)) {
      packet.result = Result::Invalid;
    } else {
      packet.result = controller_.target({az, el, true, true}, true, now);
    }
  } else if (u[0] == 'M' || u[0] == 'W') {
    bool hasEl = u[0] == 'W';
    auto args = words(text.substr(1));
    FixedVector<float, 33> nums;
    if (!args.valid) packet.result = Result::Invalid;
    for (auto a : args) {
      float value;
      if (!number(a, value)) {
        packet.result = Result::Invalid;
        break;
      }
      if (!nums.push_back(value)) { packet.result = Result::Invalid; break; }
    }
    if (packet.result == Result::Ok) {
      if (nums.size() == (hasEl ? 2u : 1u)) {
        packet.result = controller_.target({nums[0], hasEl ? nums[1] : 0, true, hasEl}, true, now);
      } else if (nums.size() >= (hasEl ? 3u : 2u) && (!hasEl || (nums.size() - 1) % 2 == 0) &&
                 nums[0] >= 1 && nums[0] <= 999 && std::floor(nums[0]) == nums[0]) {
        FixedVector<Target, 16> points;
        for (size_t i = 1; i < nums.size(); i += hasEl ? 2 : 1) {
          if (!points.push_back({nums[i], hasEl ? nums[i + 1] : 0, true, hasEl})) {
            packet.result = Result::Invalid; break;
          }
        }
        bool valid = packet.result == Result::Ok;
        for (auto t : points) {
          valid = valid && controller_.valid(t);
        }
        if (!valid) {
          packet.result = Result::Invalid;
        } else if (controller_.presetActive) {
          packet.result = Result::Ignored;
        } else {
          loadedSequence_ = points;
          loadedSequenceStarted_ = false;
          loadedStopEpoch_ = controller_.stopEpoch;
          loadedIntervalMs_ = uint32_t(nums[0]) * 1000;
          packet.result = controller_.target(points[0], true, now);
        }
      } else {
        packet.result = Result::Invalid;
      }
    }
  } else if (u == "T") {
    packet.result = controller_.sequence(loadedSequence_.data(), loadedSequence_.size(), loadedIntervalMs_, now);
    if (packet.result == Result::Ok || packet.result == Result::Pending) loadedSequenceStarted_ = true;
  } else if (u == "N") {
    packet.reply = "N=" + std::to_string(controller_.sequenceIndex()) + "/" +
                   std::to_string(loadedSequenceStarted_ ? controller_.sequenceCount() : loadedSequence_.size()) + "\r\n";
    return packet;
  } else {
    packet.matched = false;
    return packet;
  }
  if (ham) {
    packet.reply = packet.result == Result::Ok || packet.result == Result::Pending ? "RPRT 0\n"
                   : packet.result == Result::Ignored                              ? "RPRT -8\n"
                                                                                   : "RPRT -1\n";
  } else if (packet.result == Result::Invalid || packet.result == Result::NotCalibrated) {
    packet.reply = "?>\r\n";
  }
  return packet;
}
} // namespace forkrot
