#include "api.hpp"
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
namespace forkrot {
std::string jsonQuote(const std::string &s) {
  std::string out = "\"";
  for (unsigned char ch : s) {
    if (ch == '"' || ch == '\\') {
      out += '\\';
      out += ch;
    } else if (ch < 32) {
      char b[8];
      std::snprintf(b, sizeof b, "\\u%04x", ch);
      out += b;
    } else {
      out += ch;
    }
  }
  return out + '"';
}
namespace {
std::string get(const Form &form, const char *key) {
  auto it = form.find(key);
  return it == form.end() ? "" : it->second;
}
bool has(const Form &form, const char *key) {
  return form.find(key) != form.end();
}
bool integer(const std::string &s, unsigned &v, unsigned lo, unsigned hi) {
  float n;
  if (!number(s, n) || std::floor(n) != n || n < lo || n > hi) {
    return false;
  }
  v = unsigned(n);
  return true;
}
bool boolean(const std::string &s, bool &v) {
  if (s == "true" || s == "1") {
    v = true;
    return true;
  }
  if (s == "false" || s == "0") {
    v = false;
    return true;
  }
  return false;
}
bool ip(const std::string &s) {
  std::istringstream st(s);
  std::string part;
  int count = 0;
  while (std::getline(st, part, '.')) {
    unsigned v;
    if (!integer(part, v, 0, 255)) {
      return false;
    }
    ++count;
  }
  return count == 4 && !s.empty() && s.back() != '.';
}
std::string targetJson(Target t) {
  std::ostringstream json;
  json << "{\"az\":" << t.az << ",\"el\":" << t.el << ",\"hasAz\":" << (t.hasAz ? "true" : "false")
       << ",\"hasEl\":" << (t.hasEl ? "true" : "false") << '}';
  return json.str();
}
} // namespace
std::string stateJson(const Controller &controller, const Protocol &protocol,
                      const Settings &settings, uint32_t now) {
  std::ostringstream json;
  json << std::fixed << std::setprecision(2);
  json << "{\"model\":" << jsonQuote(controller.model == Model::G5500 ? "G5500" : "G800DXA")
       << ",\"controlMode\":" << jsonQuote(Controller::modeName(controller.mode))
       << ",\"remainingMs\":" << controller.remaining(now)
       << ",\"pendingValid\":" << (controller.pendingValid ? "true" : "false")
       << ",\"pending\":" << targetJson(controller.pending)
       << ",\"presetActive\":" << (controller.presetActive ? "true" : "false")
       << ",\"presetSlot\":" << controller.presetSlot
       << ",\"speedEnabled\":" << (controller.speedEnabled ? "true" : "false")
       << ",\"speedPercent\":" << controller.speedPercent
       << ",\"startSpeedPercent\":" << controller.startSpeedPercent
       << ",\"approachSpeedPercent\":" << controller.approachSpeedPercent
       << ",\"accelerationMs\":" << controller.accelerationMs
       << ",\"decelerationMs\":" << controller.decelerationMs
       << ",\"slowdownDegrees\":" << controller.slowdownDegrees
       << ",\"speedOutputPercent\":" << controller.axes[0].speedOutputPercent
       << ",\"fault\":" << jsonQuote(controller.fault)
       << ",\"rotorName\":" << jsonQuote(protocol.rotorName)
       << ",\"n1mmPort\":" << settings.n1mmPort << ",\"pstPort\":" << settings.pstPort
       << ",\"reportPort\":" << settings.reportPort
       << ",\"reportHost\":" << jsonQuote(settings.reportHost)
       << ",\"n1mmReport\":" << jsonQuote(protocol.positionReport(controller.axes[0].current))
       << ",\"presets\":[";
  for (unsigned i = 0; i < 4; ++i) {
    if (i) {
      json << ',';
    }
    json << "{\"slot\":" << i + 1 << ",\"label\":" << jsonQuote(controller.presets[i].label)
         << ",\"target\":" << targetJson(controller.presets[i].target)
         << ",\"valid\":" << (controller.valid(controller.presets[i].target) ? "true" : "false")
         << '}';
  }
  json << "]}";
  return json.str();
}
Response api(Controller &controller, Protocol &protocol, Settings &settings,
             const std::string &path, const Form &form, uint32_t now) {
  Result result = Result::Invalid;
  if (path == "/api/mode") {
    auto m = get(form, "mode");
    if (m == "manual" || m == "auto" || m == "auto_1h") {
      result = controller.setMode(m == "manual" ? Mode::Manual
                                  : m == "auto" ? Mode::Auto
                                                : Mode::AutoHour,
                                  now);
    }
  } else if (path == "/api/execute") {
    result = controller.execute(now);
  } else if (path == "/api/preset" || path == "/api/preset/run") {
    unsigned slot;
    if (integer(get(form, "slot"), slot, 1, 4)) {
      if (path == "/api/preset/run") {
        result = controller.preset(slot - 1, now);
      } else {
        float az, el = 0;
        if (number(get(form, "az"), az) &&
            (controller.model == Model::G800DXA || number(get(form, "el"), el))) {
          result = controller.savePreset(slot - 1, get(form, "label"),
                                         {az, el, true, controller.model == Model::G5500});
        }
      }
    }
  } else if (path == "/api/settings") {
    auto next = settings;
    auto name = protocol.rotorName;
    auto model = controller.model;
    bool enabled = controller.speedEnabled;
    unsigned speed = controller.speedPercent;
    bool valid = true;
    unsigned start = controller.startSpeedPercent, approach = controller.approachSpeedPercent,
             acceleration = controller.accelerationMs, deceleration = controller.decelerationMs,
             slowdown = controller.slowdownDegrees;
    if (has(form, "model")) {
      auto m = get(form, "model");
      valid = m == "G5500" || m == "G800DXA";
      model = m == "G800DXA" ? Model::G800DXA : Model::G5500;
    }
    if (has(form, "rotorName")) {
      name = get(form, "rotorName");
      valid = valid && !name.empty() && name.size() <= 31;
      for (unsigned char ch : name) {
        valid = valid && (std::isalnum(ch) || ch == '_' || ch == '-' || ch == ' ');
      }
    }
    if (has(form, "n1mmPort")) {
      valid = integer(get(form, "n1mmPort"), next.n1mmPort, 1, 65535) && valid;
    }
    if (has(form, "pstPort")) {
      valid = integer(get(form, "pstPort"), next.pstPort, 1, 65534) && valid;
    }
    if (has(form, "reportPort")) {
      valid = integer(get(form, "reportPort"), next.reportPort, 1, 65535) && valid;
    }
    if (has(form, "reportHost")) {
      next.reportHost = get(form, "reportHost");
      valid = valid && (next.reportHost.empty() || ip(next.reportHost));
    }
    if (has(form, "speedEnabled")) {
      valid = boolean(get(form, "speedEnabled"), enabled) && valid;
    }
    if (has(form, "speedPercent")) {
      valid = integer(get(form, "speedPercent"), speed, 25, 100) && valid;
    }
    if (has(form, "startSpeedPercent")) {
      valid = integer(get(form, "startSpeedPercent"), start, 25, 100) && valid;
    }
    if (has(form, "approachSpeedPercent")) {
      valid = integer(get(form, "approachSpeedPercent"), approach, 25, 100) && valid;
    }
    if (has(form, "accelerationMs")) {
      valid = integer(get(form, "accelerationMs"), acceleration, 100, 10000) && valid;
    }
    if (has(form, "decelerationMs")) {
      valid = integer(get(form, "decelerationMs"), deceleration, 100, 10000) && valid;
    }
    if (has(form, "slowdownDegrees")) {
      valid = integer(get(form, "slowdownDegrees"), slowdown, 1, 90) && valid;
    }
    valid = valid && next.n1mmPort != next.pstPort;
    if (valid) {
      if (model != controller.model) {
        controller.setModel(model);
        protocol.resetPst();
      } else if (enabled != controller.speedEnabled) {
        controller.stop();
      }
      settings = next;
      protocol.rotorName = name;
      controller.speedEnabled = enabled;
      controller.speedPercent = speed;
      controller.startSpeedPercent = start;
      controller.approachSpeedPercent = approach;
      controller.accelerationMs = acceleration;
      controller.decelerationMs = deceleration;
      controller.slowdownDegrees = slowdown;
      result = Result::Ok;
    }
  } else if (path == "/api/target") {
    float az, el = 0;
    if (number(get(form, "az"), az) && (!has(form, "el") || number(get(form, "el"), el))) {
      result = controller.target({az, el, true, has(form, "el")}, false, now);
    }
  } else if (path == "/api/move") {
    auto d = get(form, "d");
    if (d == "S") {
      controller.stop();
      result = Result::Ok;
    } else if (d == "A" || d == "E") {
      controller.stop(d == "A" ? 0 : 1);
      result = Result::Ok;
    } else if (d == "L" || d == "R" || d == "U" || d == "D") {
      result =
          controller.jog(d == "U" || d == "D" ? 1 : 0, d == "R" || d == "U" ? 1 : -1, false, now);
    }
  }
  return {result == Result::Ok || result == Result::Pending ? 200
          : result == Result::Ignored                       ? 409
                                                            : 400,
          std::string("{\"ok\":") +
              (result == Result::Ok || result == Result::Pending ? "true" : "false") +
              ",\"result\":" + jsonQuote(Controller::resultName(result)) + "}"};
}
Form decodeForm(const std::string &input) {
  auto decode = [](const std::string &s) {
    std::string json;
    for (size_t i = 0; i < s.size(); ++i) {
      if (s[i] == '+') {
        json += ' ';
      } else if (s[i] == '%' && i + 2 < s.size() && std::isxdigit((unsigned char)s[i + 1]) &&
                 std::isxdigit((unsigned char)s[i + 2])) {
        json += char(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
        i += 2;
      } else {
        json += s[i];
      }
    }
    return json;
  };
  Form form;
  std::istringstream st(input);
  std::string part;
  while (std::getline(st, part, '&')) {
    auto pos = part.find('=');
    if (pos != std::string::npos) {
      form[decode(part.substr(0, pos))] = decode(part.substr(pos + 1));
    }
  }
  return form;
}
} // namespace forkrot
