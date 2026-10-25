#include "api.hpp"
#include "motion_json.hpp"
#include "board.hpp"
#include "http_request_gate.hpp"
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
bool externalAzMode(std::string_view s, ExternalAzMode &mode) {
  if (s != "shortest" && s != "mechanical") return false;
  mode = s == "mechanical" ? ExternalAzMode::Mechanical : ExternalAzMode::Shortest;
  return true;
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
       << ",\"azProfile\":" << jsonQuoted(azProfileName(controller.azProfile))
       << ",\"azStart\":" << controller.minimum << ",\"azSpan\":" << controller.azRange.span()
       << ",\"azMax\":" << controller.maximum << ",\"azRangeRevision\":" << controller.rangeRevision
    << ",\"elProfile\":" << jsonQuoted(elProfileName(controller.elProfile))
    << ",\"elStart\":" << controller.elRange.start << ",\"elSpan\":" << controller.elRange.span()
    << ",\"elMax\":" << controller.elRange.end << ",\"elRangeRevision\":" << controller.rangeRevision
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
  json << ",\"autotune\":";writeAutoTune(json,controller);
  json << ",\"moving\":" << (controller.moving() ? "true" : "false")
       << ",\"setupRequired\":" << (controller.setupRequired ? "true" : "false")
       << ",\"configurationLocked\":" << (controller.configurationLocked ? "true" : "false")
       << ",\"presetActive\":" << (controller.presetActive ? "true" : "false")
       << ",\"presetSlot\":" << controller.presetSlot << ",\"speedEnabled\":"
       << (((controller.remote || controller.hardware.simulation)
                ? controller.variableSpeed(0)
                : (controller.hardware.drive != DRIVE_YAESU || controller.speedEnabled))
               ? "true"
               : "false")
       << ",\"speedPercent\":" << reportedSpeed(controller,0).speedPercent
       << ",\"startSpeedPercent\":" << reportedSpeed(controller,0).startSpeedPercent
       << ",\"approachSpeedPercent\":" << reportedSpeed(controller,0).approachSpeedPercent
       << ",\"accelerationMs\":" << reportedSpeed(controller,0).accelerationMs
       << ",\"decelerationMs\":" << reportedSpeed(controller,0).decelerationMs
       << ",\"slowdownDegrees\":" << reportedSpeed(controller,0).slowdownDegrees
       << ",\"coastDegrees\":" << reportedSpeed(controller,0).coastDegrees
       << ",\"decelerationFactor\":" << reportedSpeed(controller,0).decelerationFactor
       << ",\"speedOutputPercent\":" << controller.axes[0].speedOutputPercent
       << ",\"fault\":" << jsonQuoted(controller.fault)
       << ",\"rotorName\":" << jsonQuoted(protocol.rotorName)
       << ",\"n1mmPort\":" << settings.n1mmPort << ",\"pstPort\":" << settings.pstPort
       << ",\"n1mmManualFollow\":" << (protocol.manualFollow.n1mm ? "true" : "false")
       << ",\"pstManualFollow\":" << (protocol.manualFollow.pst ? "true" : "false")
       << ",\"gs232ManualFollow\":" << (protocol.manualFollow.gs232 ? "true" : "false")
       << ",\"rotctldManualFollow\":" << (protocol.manualFollow.rotctld ? "true" : "false")
       << ",\"externalAzMode\":\""
       << (protocol.externalAzMode == ExternalAzMode::Mechanical ? "mechanical" : "shortest") << '"'
       << ",\"reportPort\":" << settings.reportPort << ",\"commandPort\":" << settings.commandPort
       << ",\"reportHost\":" << jsonQuoted(settings.reportHost) << ",\"n1mmReport\":\""
       << jsonEscaped(protocol.rotorName) << '@'
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
  json << "],\"build\":{\"board\":" << jsonQuoted(board::NAME) << ",\"display\":"
       << jsonQuoted(DISPLAY_KIND == DISPLAY_CROWPANEL_21 ? "CROWPANEL_21"
                     : DISPLAY_KIND == DISPLAY_ILI9341    ? "ILI9341"
                     : DISPLAY_KIND == DISPLAY_LCD1602    ? "LCD1602"
                                                          : "NONE")
       << ",\"bluetooth\":" << (controller.hardware.bluetooth ? "true" : "false")
       << ",\"bluetoothBackend\":" << jsonQuoted(controller.hardware.backendName()) << ",\"drive\":"
       << jsonQuoted(controller.remote                      ? "HUD"
                     : controller.hardware.simulation                     ? "SIMULATED"
                     : !controller.hardware.hasDirections(0) && !controller.hardware.hasDirections(1) ? "NONE"
                     : controller.hardware.drive == DRIVE_YAESU       ? "YAESU"
                     : controller.hardware.drive == DRIVE_BTS7960     ? "BTS7960"
                                                            : "SEPARATE_PWM")
       << ",\"singleAxis\":" << (controller.hardware.pins.singleAxis() && (!FORK_C3 || controller.hardware.localAxisConfigured(0)) && !controller.remote && !controller.hardware.simulation ? "true" : "false")
       << ",\"userButtonGpio\":" << int(controller.hardware.buttonGpio)
       << ",\"userButtonActiveLow\":" << (controller.hardware.buttonLow ? "true" : "false")
       << ",\"userButtonPullup\":" << (controller.hardware.buttonPullup ? "true" : "false")
       << ",\"pins\":{\"azAdc\":" << controller.hardware.pins.azAdc << ",\"elAdc\":" << controller.hardware.pins.elAdc
       << ",\"azNegative\":" << controller.hardware.pins.azNegative << ",\"azPositive\":" << controller.hardware.pins.azPositive
       << ",\"elNegative\":" << controller.hardware.pins.elNegative << ",\"elPositive\":" << controller.hardware.pins.elPositive
       << ",\"azSpeedEnable\":" << controller.hardware.pins.azSpeedEnable << ",\"elSpeedEnable\":" << controller.hardware.pins.elSpeedEnable
       << "}},\"axes\":[";
  for (unsigned i=0; i<2; ++i) {
    if (i) json << ',';
    const auto &axis = controller.axes[i];
    json << "{\"current\":" << axis.current << ",\"target\":" << axis.target
         << ",\"tracking\":" << (axis.tracking ? "true" : "false")
         << ",\"completed\":" << (axis.completed ? "true" : "false")
         << ",\"calibrated\":" << (axis.calibrated ? "true" : "false") << ",\"direction\":" << axis.direction
         << ",\"stopToleranceDegrees\":" << controller.effectiveTolerance(i)
         << ",\"speedEnabled\":" << (controller.variableSpeed(i) ? "true" : "false")
         << ",\"speedOutputPercent\":" << axis.speedOutputPercent;
    json << ",\"speedSetpoint\":";json.decimal(controller.runtimeSpeed(i),-1) << ",\"speedLimit\":";
    json.decimal(controller.runtimeSpeedLimit(i),-1)
         << ",\"speedAdjustable\":" << (controller.runtimeSpeedAvailable(i) ? "true" : "false");
    writeAxisMotion(json,controller,i); json << '}';
  }
  json << ']';
  for (unsigned i=0; i<2; ++i) {
    const auto &speed = reportedSpeed(controller,i);
    const char *prefix = i ? "el" : "az";
    json << ",\"" << prefix << "SpeedPercent\":" << speed.speedPercent;
    json << ",\"" << prefix << "StartSpeedPercent\":" << speed.startSpeedPercent;
    json << ",\"" << prefix << "ApproachSpeedPercent\":" << speed.approachSpeedPercent;
    json << ",\"" << prefix << "AccelerationMs\":" << speed.accelerationMs;
    json << ",\"" << prefix << "DecelerationMs\":" << speed.decelerationMs;
    json << ",\"" << prefix << "SlowdownDegrees\":" << speed.slowdownDegrees;
    json << ",\"" << prefix << "CoastDegrees\":" << speed.coastDegrees;
    json << ",\"" << prefix << "DecelerationFactor\":" << speed.decelerationFactor;
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
  if(controller.autoTune.active() && path=="/api/settings")
    return {503,"{\"ok\":false,\"result\":\"busy\"}"};
  if (has(form, "rangeRevision") && !(path == "/api/move" && (get(form, "d") == "S" || get(form, "d") == "A" || get(form, "d") == "E"))) {
    uint32_t revision;
    if (!requestGeneration(get(form, "rangeRevision"), revision) || revision != controller.rangeRevision)
      return {409, "{\"ok\":false,\"result\":\"cancelled\"}"};
  }
  if (controller.remote && has(form, "selection")) {
    uint32_t selection;
    if (!requestGeneration(get(form, "selection"), selection) ||
        selection != controller.remote->selectionGeneration())
      return {409, "{\"ok\":false,\"result\":\"cancelled\"}"};
  }
  const uint32_t before = controller.remote ? controller.remote->requestId() : 0;
  if (controller.remote && path == "/api/settings") {
    auto next = settings;
    auto name = protocol.rotorName;
    auto follow = protocol.manualFollow;
    auto azMode = protocol.externalAzMode;
    for (const auto &entry : form) {
      const auto key = entry.first, value = entry.second;
      bool ok = false;
      if (key == "rotorName") {
        name = value;
        ok = !name.empty() && name.size() <= 31;
        for (unsigned char ch : name)
          ok = ok && (std::isalnum(ch) || ch == '_' || ch == '-' || ch == ' ');
      } else if (key == "n1mmPort")
        ok = integer(value, next.n1mmPort, 1, 65535);
      else if (key == "pstPort")
        ok = integer(value, next.pstPort, 1, 65534);
      else if (key == "commandPort")
        ok = integer(value, next.commandPort, 1, 65535);
      else if (key == "reportPort")
        ok = integer(value, next.reportPort, 1, 65535);
      else if (key == "reportHost") {
        next.reportHost = value;
        ok = value.empty() || ip(value);
      } else if (key == "encoderInverted")
        ok = boolean(value, next.encoderInverted);
      else if (key == "n1mmManualFollow")
        ok = boolean(value, follow.n1mm);
      else if (key == "pstManualFollow")
        ok = boolean(value, follow.pst);
      else if (key == "gs232ManualFollow")
        ok = boolean(value, follow.gs232);
      else if (key == "rotctldManualFollow")
        ok = boolean(value, follow.rotctld);
      else if (key == "externalAzMode")
        ok = externalAzMode(value, azMode);
      if (!ok)
        return {400, "{\"ok\":false,\"result\":\"controller_settings_only\"}"};
    }
    if(!commandPortAllowed(next.commandPort) || next.n1mmPort==next.pstPort || next.commandPort==next.n1mmPort || next.commandPort==next.pstPort || next.commandPort==next.pstPort+1 || next.commandPort==next.reportPort)return {400,"{\"ok\":false,\"result\":\"invalid\"}"};
    settings = next;
    protocol.rotorName = name;
    protocol.manualFollow = follow;
    protocol.setExternalAzMode(azMode);
    return {200, "{\"ok\":true,\"result\":\"ok\"}"};
  }
  if (path == "/api/speed") {
    const auto axis=get(form,"axis");float value;
    if((axis=="az" || axis=="el") && number(get(form,"value"),value))
      result=controller.setRuntimeSpeed(axis=="az"?0:1,value,now);
  } else if (path == "/api/mode") {
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
    auto follow = protocol.manualFollow;
    auto azMode = protocol.externalAzMode;
    auto model = controller.model;
    bool enabled = controller.speedEnabled;
    auto speeds = controller.speeds;
    auto motion = controller.motionSettings;
    unsigned controlHz=1000/controller.controlPeriodMs();
    bool motionTouched[2]{};
    bool valid = true;
    if(has(form,"controlHz"))valid=integer(get(form,"controlHz"),controlHz,50,200)&&validMotionHz(controlHz);
    // Compile-time hardware configuration is read-only, including attempted aliases.
    for (const auto &entry : form) {
      if (entry.first == "drive" || entry.first == "display" || entry.first == "bluetooth" || entry.first == "bluetoothBackend" || entry.first == "pins" || entry.first == "build" || entry.first == "board" || entry.first == "boardProfile" ||
          entry.first == "displayKind" || entry.first == "bluetoothEnabled" || entry.first == "driveInterface" ||
          entry.first.find("userButton") == 0 || entry.first.find("build.") == 0 || entry.first.find("pin.") == 0 ||
          entry.first == "buttonGpio" || entry.first == "buttonActiveLow" || entry.first == "buttonPullup" ||
          entry.first == "simulation" || entry.first == "bluetoothMode" || entry.first == "forceApSta" || entry.first == "pwmInverted") valid = false;
    }
    if (has(form, "model")) {
      auto m = get(form, "model");
      valid = (m == "G5500" || m == "G800DXA") && valid;
      model = m == "G800DXA" ? Model::G800DXA : Model::G5500;
      if (!controller.remote && !controller.hardware.simulation && controller.hardware.pins.singleAxis() && (!FORK_C3 || controller.hardware.localAxisConfigured(0)) && model != Model::G800DXA) valid = false;
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
    if (has(form, "commandPort")) valid = integer(get(form, "commandPort"), next.commandPort, 1, 65535) && valid;
    if (has(form, "reportPort")) {
      valid = integer(get(form, "reportPort"), next.reportPort, 1, 65535) && valid;
    }
    for (auto entry : {std::pair<const char *, bool *>{"n1mmManualFollow", &follow.n1mm},
                       {"pstManualFollow", &follow.pst}, {"gs232ManualFollow", &follow.gs232},
                       {"rotctldManualFollow", &follow.rotctld}}) {
      if (has(form, entry.first))
        valid = boolean(get(form, entry.first), *entry.second) && valid;
    }
    if (has(form, "externalAzMode"))
      valid = externalAzMode(get(form, "externalAzMode"), azMode) && valid;
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
    if (controller.hardware.drive != DRIVE_YAESU && !enabled) valid = false;
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
      const unsigned minimum = 1;
      value("speedPercent", speed.speedPercent, minimum, 100);
      value("startSpeedPercent", speed.startSpeedPercent, minimum, 100);
      value("approachSpeedPercent", speed.approachSpeedPercent, minimum, 100);
      value("accelerationMs", speed.accelerationMs, 100, 10000);
      value("decelerationMs", speed.decelerationMs, 100, 10000);
      value("slowdownDegrees", speed.slowdownDegrees, 1, 90);
      auto realValue = [&](const char *legacy, float &field, float lo, float hi) {
        char key[32];
        std::snprintf(key, sizeof key, "%s%c%s", prefix, std::toupper(legacy[0]), legacy + 1);
        float aliased = field, prefixed = field;
        const bool old = i == 0 && has(form, legacy), modern = has(form, key);
        if (old) valid = (number(get(form, legacy), aliased) && aliased >= lo && aliased <= hi) && valid;
        if (modern) valid = (number(get(form, key), prefixed) && prefixed >= lo && prefixed <= hi) && valid;
        if (old && modern && aliased != prefixed) valid = false;
        if (modern || old) field = modern ? prefixed : aliased;
      };
      realValue("coastDegrees", speed.coastDegrees, 0, 90);
      realValue("decelerationFactor", speed.decelerationFactor, 0.1f, 10);
      auto &m=motion[i]; char key[48];
      std::snprintf(key,sizeof key,"%sRegulatorMode",prefix);
      if (has(form,key)) {
        motionTouched[i]=true; const auto mode=get(form,key);
        valid=(mode=="simple" || mode=="cascade") && valid;
        m.mode=mode=="cascade"?RegulatorMode::Cascade:RegulatorMode::Simple;
      }
      for (auto field : {std::pair<const char *,bool *>{"Ready",&m.ready},{"KickEnabled",&m.kickEnabled},{"HoldEnabled",&m.holdEnabled}}) {
        std::snprintf(key,sizeof key,"%sMotion%s",prefix,field.first);
        if (has(form,key)) { motionTouched[i]=true; valid=boolean(get(form,key),*field.second)&&valid; }
      }
      for (const auto &f:motionRealFields) {
        std::snprintf(key,sizeof key,"%sMotion%s",prefix,f.name);
        if (has(form,key)) { motionTouched[i]=true; float n;
          const bool ok=number(get(form,key),n)&&n>=f.lo&&n<=f.hi; valid=ok&&valid;
          if(ok) m.*f.member=n;
        }
      }
      for (const auto &f:motionIntFields) {
        std::snprintf(key,sizeof key,"%sMotion%s",prefix,f.name);
        if(has(form,key)) { motionTouched[i]=true; valid=integer(get(form,key),m.*f.member,f.lo,f.hi)&&valid; }
      }
      if(motionTouched[i]) m.ready=true; // legacy MotionReady is accepted, never an authorization gate
      valid=m.valid()&&valid;
      const bool variable=controller.hardware.simulation || (controller.hardware.drive==DRIVE_YAESU
          ? i==0 && model==Model::G800DXA && enabled : i==0 || model==Model::G5500);
      if(motionTouched[i] && m.mode==RegulatorMode::Cascade && !variable) valid=false;

    }
    valid = valid && commandPortAllowed(next.commandPort) && next.n1mmPort != next.pstPort && next.commandPort != next.n1mmPort && next.commandPort != next.pstPort && next.commandPort != next.pstPort+1 && next.commandPort != next.reportPort;
    if(model!=controller.model && (motionTouched[0] || motionTouched[1])) valid=false;
    if(valid && (motion[0].ready || motion[1].ready) && !prepareMotionPlanner())
      return {503,"{\"ok\":false,\"result\":\"unavailable\"}"};
    if (valid) {
      if(controlHz!=1000/controller.controlPeriodMs() && controller.setControlFrequency(controlHz,now)!=Result::Ok)
        return {503,"{\"ok\":false,\"result\":\"unavailable\"}"};
      if (model != controller.model) {
        controller.setModel(model);
        protocol.resetPst();
      } else if (enabled != controller.speedEnabled) {
        controller.stop();
      }
      settings = next;
      protocol.rotorName = name;
      protocol.manualFollow = follow;
      protocol.setExternalAzMode(azMode);
      controller.speedEnabled = enabled;
      for(unsigned i=0;i<2;++i) {
        if (!(controller.speeds[i]==speeds[i])) { controller.stopNow(int(i),now); controller.resetRuntimeSpeed(i,now); }
        controller.configureMotion(i,motion[i],now);
      }
      controller.speeds = speeds;
      result = Result::Ok;
    }
  } else if (path == "/api/motion/autotune") {
    if(controller.remote)return {409,"{\"ok\":false,\"result\":\"controller_settings_only\"}"};
    const auto action=get(form,"action");
    if(action=="center") {
      const auto axis=get(form,"axis");
      if(axis=="az" || axis=="el")result=controller.centerForAutoTune(axis=="az"?0:1,now);
    } else if(action=="cancel") {
      uint32_t expected;
      if(!has(form,"id"))result=controller.cancelAutoTune(now);
      else if(requestGeneration(get(form,"id"),expected))
        result=expected==controller.autoTune.id?controller.cancelAutoTune(now):Result::Unavailable;
    } else if(action=="apply") {
      uint32_t id;
      if(requestGeneration(get(form,"id"),id)) result=controller.applyAutoTune(id,now);
    } else if(action=="start") {
      const auto axis=get(form,"axis"),mode=get(form,"mode");float travel,limit;uint32_t previous;
      AutoTuneOptions options;bool optionsValid=true;
      for(const auto &field:autoTuneRealFields)if(has(form,field.name)) {
        float value;
        if(!number(get(form,field.name),value) || value<field.lo || value>field.hi)optionsValid=false;
        else options.*field.member=value;
      }
      for(const auto &field:autoTuneIntFields)if(has(form,field.name)) {
        unsigned value;
        if(!integer(get(form,field.name),value,field.lo,field.hi))optionsValid=false;
        else options.*field.member=value;
      }
      // A misspelled methodology must not silently run the default experiment.
      for(const auto &entry:form)if(entry.first.substr(0,4)=="tune") {
        bool known=false;
        for(const auto &field:autoTuneRealFields)known|=entry.first==field.name;
        for(const auto &field:autoTuneIntFields)known|=entry.first==field.name;
        if(!known)optionsValid=false;
      }
      if(optionsValid && (axis=="az" || axis=="el") && (mode=="simple" || mode=="cascade") &&
          requestGeneration(get(form,"id"),previous) && previous==controller.autoTune.id &&
          number(get(form,"travel"),travel) && number(get(form,"outputLimit"),limit))
        result=controller.startAutoTune(axis=="az"?0:1,mode=="simple"?RegulatorMode::Simple:RegulatorMode::Cascade,travel,limit,now,options);
    }
  } else if (path == "/api/motion/reset") {
    const auto axis=get(form,"axis");
    if(axis=="az" || axis=="el" || axis=="all") result=controller.resetMotion(axis=="all"?-1:axis=="az"?0:1,now);
  } else if (path == "/api/target") {
    float az = 0, el = 0;
    const bool hasAz = has(form, "az"), hasEl = has(form, "el");
    if ((hasAz || hasEl) && (!hasAz || number(get(form, "az"), az)) &&
        (!hasEl || number(get(form, "el"), el))) {
      result = controller.target({az, el, hasAz, hasEl}, false, now);
    }
  } else if (path == "/api/move") {
    auto d = get(form, "d");
    const bool held = d == "H" || has(form,"client") || has(form,"session") ||
        has(form,"gesture") || has(form,"serial") || has(form,"mask");
    if (held) {
      uint32_t client,session,gesture,serial,mask;float az,el;
      if ((d=="H" || d=="A" || d=="E" || d=="S") &&
          requestGeneration(get(form,"client"),client) && client &&
          requestGeneration(get(form,"session"),session) && session &&
          requestGeneration(get(form,"gesture"),gesture) && gesture &&
          requestGeneration(get(form,"serial"),serial) && serial &&
          requestGeneration(get(form,"mask"),mask) && mask>0 && mask<=3 &&
          number(get(form,"az"),az) && number(get(form,"el"),el) &&
          (d=="H" || (az==0 && el==0)) &&
          (d!="A" || mask==1) && (d!="E" || mask==2))
        result=controller.manualDrive(client,session,gesture,serial,uint8_t(mask),az,el,d=="S",now,1000);
    } else if (d == "S") {
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
  if (controller.remote && controller.remote->requestId() != before)
    result = controller.remote->lastResult();
  if (result == Result::Deferred)
    return {202, "{\"ok\":false,\"result\":\"deferred\",\"requestId\":" +
                     std::to_string(controller.remote->requestId()) + "}"};
  return {result == Result::Unavailable || result == Result::Busy ? 503
          : result == Result::Ok || result == Result::Pending     ? 200
          : result == Result::Ignored                             ? 409
                                                                  : 400,
          std::string("{\"ok\":") + (result == Result::Ok || result == Result::Pending ? "true" : "false") +
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
