#include "api.hpp"
#include "board.hpp"
#include <array>
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
std::string_view get(FormView form, const char *key) {
  // Match the previous map adapter: the last duplicate wins.
  for (size_t i = form.count; i; --i) if (form.entries[i - 1].first == key) return form.entries[i - 1].second;
  return {};
}
bool has(FormView form, const char *key) {
  for (const auto &entry : form) if (entry.first == key) return true;
  return false;
}
bool integer(std::string_view s, unsigned &v, unsigned lo, unsigned hi) {
  float n;
  if (!number(s, n) || std::floor(n) != n || n < lo || n > hi) {
    return false;
  }
  v = unsigned(n);
  return true;
}
bool boolean(std::string_view s, bool &v) {
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
bool ip(std::string_view s) {
  for (unsigned i = 0; i < 4; ++i) {
    auto dot = s.find('.');
    unsigned value;
    if (!integer(s.substr(0, dot), value, 0, 255)) return false;
    if (i == 3) return dot == s.npos;
    if (dot == s.npos) return false;
    s.remove_prefix(dot + 1);
  }
  return false;
}
void writeTargetJson(JsonWriter &json, Target t) {
  json << "{\"az\":";
  json.decimal(t.az, -1) << ",\"el\":";
  json.decimal(t.el, -1) << ",\"hasAz\":" << (t.hasAz ? "true" : "false")
       << ",\"hasEl\":" << (t.hasEl ? "true" : "false") << '}';
}
} // namespace
bool writeStateJson(JsonWriter &json, const Controller &controller, const Protocol &protocol,
                    const Settings &settings, uint32_t now, bool appendMembers) {
  json << (appendMembers ? ",\"model\":" : "{\"model\":") << jsonQuoted(controller.model == Model::G5500 ? "G5500" : "G800DXA")
       << ",\"controlMode\":" << jsonQuoted(Controller::modeName(controller.mode))
#if SELECTED_BOARD == BOARD_CROWPANEL_21
       << ",\"encoderInverted\":" << (settings.encoderInverted ? "true" : "false")
#endif
       << ",\"remainingMs\":" << controller.remaining(now)
       << ",\"pendingValid\":" << (controller.pendingValid ? "true" : "false")
       << ",\"pending\":";
  writeTargetJson(json, controller.pending);
  json << ",\"lastTarget\":";
  writeTargetJson(json, controller.lastTarget);
  json << ",\"moving\":" << (controller.moving() ? "true" : "false")
       << ",\"presetActive\":" << (controller.presetActive ? "true" : "false")
       << ",\"presetSlot\":" << controller.presetSlot
       << ",\"speedEnabled\":" << ((DRIVE_INTERFACE != DRIVE_YAESU || controller.speedEnabled) ? "true" : "false")
       << ",\"speedPercent\":" << controller.speedPercent
       << ",\"startSpeedPercent\":" << controller.startSpeedPercent
       << ",\"approachSpeedPercent\":" << controller.approachSpeedPercent
       << ",\"accelerationMs\":" << controller.accelerationMs
       << ",\"decelerationMs\":" << controller.decelerationMs
       << ",\"slowdownDegrees\":" << controller.slowdownDegrees
       << ",\"speedOutputPercent\":" << controller.axes[0].speedOutputPercent
       << ",\"fault\":" << jsonQuoted(controller.fault)
       << ",\"rotorName\":" << jsonQuoted(protocol.rotorName)
       << ",\"n1mmPort\":" << settings.n1mmPort << ",\"pstPort\":" << settings.pstPort
       << ",\"reportPort\":" << settings.reportPort
       << ",\"reportHost\":" << jsonQuoted(settings.reportHost)
       << ",\"n1mmReport\":\"" << jsonEscaped(protocol.rotorName) << '@'
       << int(std::lround(heading(controller.axes[0].current) * 10)) % 3600 << '"'
       << ",\"presets\":[";
  for (unsigned i = 0; i < 4; ++i) {
    if (i) {
      json << ',';
    }
    json << "{\"slot\":" << i + 1 << ",\"label\":" << jsonQuoted(controller.presets[i].label)
         << ",\"target\":";
    writeTargetJson(json, controller.presets[i].target);
    json << ",\"valid\":" << (controller.valid(controller.presets[i].target) ? "true" : "false")
         << '}';
  }
  json << "],\"build\":{\"board\":" << jsonQuoted(board::NAME)
       << ",\"display\":" << jsonQuoted(DISPLAY_KIND == DISPLAY_CROWPANEL_21 ? "CROWPANEL_21" : DISPLAY_KIND == DISPLAY_ILI9341 ? "ILI9341" : DISPLAY_KIND == DISPLAY_LCD1602 ? "LCD1602" : "NONE")
       << ",\"bluetooth\":" << (BLUETOOTH_ENABLED ? "true" : "false")
       << ",\"bluetoothBackend\":" << jsonQuoted(board::BLUETOOTH_BACKEND_NAME)
       << ",\"drive\":" << jsonQuoted(SELECTED_BOARD == BOARD_CROWPANEL_21 ? (CROWPANEL_SIMULATION ? "SIMULATED" : "NONE") : DRIVE_INTERFACE == DRIVE_YAESU ? "YAESU" : DRIVE_INTERFACE == DRIVE_BTS7960 ? "BTS7960" : "SEPARATE_PWM")
       << ",\"userButtonGpio\":" << USER_BUTTON_GPIO << ",\"userButtonActiveLow\":" << (USER_BUTTON_ACTIVE_LOW ? "true" : "false")
       << ",\"userButtonPullup\":" << (USER_BUTTON_PULLUP ? "true" : "false")
       << ",\"pins\":{\"azAdc\":" << board::PIN_AZ_ADC << ",\"elAdc\":" << board::PIN_EL_ADC
       << ",\"azNegative\":" << board::PIN_LEFT << ",\"azPositive\":" << board::PIN_RIGHT
       << ",\"elNegative\":" << board::PIN_DOWN << ",\"elPositive\":" << board::PIN_UP
       << ",\"azSpeedEnable\":" << board::PIN_SPEED << ",\"elSpeedEnable\":" << board::PIN_EL_SPEED << "}},\"axes\":[";
  for (unsigned i=0; i<2; ++i) {
    if (i) json << ',';
    const auto &axis = controller.axes[i];
    json << "{\"current\":" << axis.current << ",\"target\":" << axis.target
         << ",\"tracking\":" << (axis.tracking ? "true" : "false")
         << ",\"completed\":" << (axis.completed ? "true" : "false")
         << ",\"calibrated\":" << (axis.calibrated ? "true" : "false")
         << ",\"direction\":" << axis.direction << ",\"stopToleranceDegrees\":" << Controller::tolerance(i)
         << ",\"speedEnabled\":" << (controller.variableSpeed(i) ? "true" : "false")
         << ",\"speedOutputPercent\":" << axis.speedOutputPercent << '}';
  }
  json << ']';
  for (unsigned i=0; i<2; ++i) {
    const auto &speed = controller.speeds[i];
    const char *prefix = i ? "el" : "az";
    json << ",\"" << prefix << "SpeedPercent\":" << speed.speedPercent;
    json << ",\"" << prefix << "StartSpeedPercent\":" << speed.startSpeedPercent;
    json << ",\"" << prefix << "ApproachSpeedPercent\":" << speed.approachSpeedPercent;
    json << ",\"" << prefix << "AccelerationMs\":" << speed.accelerationMs;
    json << ",\"" << prefix << "DecelerationMs\":" << speed.decelerationMs;
    json << ",\"" << prefix << "SlowdownDegrees\":" << speed.slowdownDegrees;
    json << ",\"" << prefix << "SpeedEnabled\":" << (controller.variableSpeed(i) ? "true" : "false");
    json << ",\"" << prefix << "SpeedOutputPercent\":" << controller.axes[i].speedOutputPercent;
  }
  json << '}';

  return json.good();
}
std::string stateJson(const Controller &controller, const Protocol &protocol,
                      const Settings &settings, uint32_t now) {
  char buffer[STATUS_JSON_CAPACITY];
  JsonWriter json(buffer, sizeof buffer);
  if (!writeStateJson(json, controller, protocol, settings, now)) return "{\"error\":\"status_too_large\"}";
  return std::string(json.data(), json.size());
}
Response apiView(Controller &controller, Protocol &protocol, Settings &settings,
                 std::string_view path, FormView form, uint32_t now) {
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
          result = controller.savePreset(slot - 1, std::string(get(form, "label")),
                                         {az, el, true, controller.model == Model::G5500});
        }
      }
    }
  } else if (path == "/api/settings") {
    auto next = settings;
    auto name = protocol.rotorName;
    auto model = controller.model;
    bool enabled = controller.speedEnabled;
    auto speeds = controller.speeds;
    bool valid = true;
    // Compile-time hardware configuration is read-only, including attempted aliases.
    for (const auto &entry : form) {
      if (entry.first == "drive" || entry.first == "display" || entry.first == "bluetooth" || entry.first == "bluetoothBackend" || entry.first == "pins" || entry.first == "build" || entry.first == "board" || entry.first == "boardProfile" ||
          entry.first == "displayKind" || entry.first == "bluetoothEnabled" || entry.first == "driveInterface" ||
          entry.first.find("userButton") == 0 || entry.first.find("build.") == 0) valid = false;
    }
    if (has(form, "model")) {
      auto m = get(form, "model");
      valid = (m == "G5500" || m == "G800DXA") && valid;
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
    if (has(form, "speedEnabled")) valid = boolean(get(form, "speedEnabled"), enabled) && valid;
    if (has(form, "encoderInverted")) {
#if SELECTED_BOARD == BOARD_CROWPANEL_21
      valid = boolean(get(form, "encoderInverted"), next.encoderInverted) && valid;
#else
      valid = false;
#endif
    }
    if (has(form, "azSpeedEnabled")) {
      bool alias = enabled;
      valid = boolean(get(form, "azSpeedEnabled"), alias) && valid;
      if (has(form, "speedEnabled") && alias != enabled) valid = false;
      enabled = alias;
    }
    if (DRIVE_INTERFACE != DRIVE_YAESU && !enabled) valid = false;
    if (has(form, "elSpeedEnabled")) valid = false; // PWM is part of the compiled interface.

    for (unsigned i=0; i<2; ++i) {
      auto &speed = speeds[i];
      const char *prefix = i ? "el" : "az";
      auto value = [&](const char *legacy, unsigned &field, unsigned lo, unsigned hi) {
        char key[32];
        std::snprintf(key, sizeof key, "%s%c%s", prefix, std::toupper(legacy[0]), legacy + 1);
        unsigned aliased = field, prefixed = field;
        const bool old = i == 0 && has(form, legacy), modern = has(form, key);
        if (old) valid = integer(get(form, legacy), aliased, lo, hi) && valid;
        if (modern) valid = integer(get(form, key), prefixed, lo, hi) && valid;
        if (old && modern && aliased != prefixed) valid = false;
        if (modern || old) field = modern ? prefixed : aliased;
      };
      const unsigned minimum = DRIVE_INTERFACE == DRIVE_YAESU && i == 0 ? 25 : 1;
      value("speedPercent", speed.speedPercent, minimum, 100);
      value("startSpeedPercent", speed.startSpeedPercent, minimum, 100);
      value("approachSpeedPercent", speed.approachSpeedPercent, minimum, 100);
      value("accelerationMs", speed.accelerationMs, 100, 10000);
      value("decelerationMs", speed.decelerationMs, 100, 10000);
      value("slowdownDegrees", speed.slowdownDegrees, 1, 90);
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
      controller.speeds = speeds;
      result = Result::Ok;
    }
  } else if (path == "/api/target") {
    float az = 0, el = 0;
    const bool hasAz = has(form, "az"), hasEl = has(form, "el");
    if ((hasAz || hasEl) && (!hasAz || number(get(form, "az"), az)) &&
        (!hasEl || number(get(form, "el"), el))) {
      result = controller.target({az, el, hasAz, hasEl}, false, now);
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
Response api(Controller &controller, Protocol &protocol, Settings &settings,
             const std::string &path, const Form &form, uint32_t now) {
  std::array<FormArgument, 32> arguments;
  if (form.size() > arguments.size()) return {400, "{\"ok\":false,\"result\":\"invalid\"}"};
  size_t count = 0;
  for (const auto &entry : form) arguments[count++] = {entry.first, entry.second};
  return apiView(controller, protocol, settings, path, {arguments.data(), count}, now);
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
  size_t start = 0;
  while (start < input.size()) {
    auto end = input.find('&', start);
    auto part = input.substr(start, end == input.npos ? input.size() - start : end - start);
    auto pos = part.find('=');
    if (pos != std::string::npos) form[decode(part.substr(0, pos))] = decode(part.substr(pos + 1));
    if (end == input.npos) break;
    start = end + 1;
  }
  return form;
}
} // namespace forkrot
