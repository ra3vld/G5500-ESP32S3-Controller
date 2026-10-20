// Device configuration and control shared by HTTP, UART/USB, TCP and UDP. GPL-3.0.
#include "device_api.hpp"
#include "command_api.hpp"
#include "hardware_form.hpp"
#include "motion_json.hpp"
#include "runtime_metrics.hpp"
#include "tcp_memory.hpp"
#include <esp_timer.h>
namespace forkrot {
using namespace board;
namespace {
// Compatibility surface for the existing API bodies; never registers handlers
// or owns request/response buffers. The caller owns all transport memory.
class ApiContext {
  FormView form_; JsonWriter &out_; int &status_; const char *&type_;
public:
  ApiContext(FormView form, JsonWriter &out, int &status, const char *&type)
      : form_(form), out_(out), status_(status), type_(type) {}
  std::string_view value(std::string_view key) const {
    for (auto entry : form_) if (entry.first == key) return entry.second;
    return {};
  }
  String arg(std::string_view key) const { auto v=value(key); return String(v.data(),v.size()); }
  bool hasArg(std::string_view key) const { for(auto e:form_) if(e.first==key)return true; return false; }
  void send(int status, const char *type, const char *body) { finish(status,type); out_ << body; }
  void send(int status, const char *type, const String &body) { finish(status,type); out_ << std::string_view(body.c_str(),body.length()); }
  void finish(int status, const char *type) { status_=status; type_=type; }
  void sendHeader(const char *, const char *) {}
};
}
namespace {
bool nodeId(const String &text, uint64_t &id) {
  if (!text.length() || text.length() > 12)
    return false;
  id = 0;
  for (size_t i = 0; i < text.length(); ++i) {
    const char c = text.c_str()[i];
    int n = c >= '0' && c <= '9'   ? c - '0'
            : c >= 'a' && c <= 'f' ? c - 'a' + 10
            : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                   : -1;
    if (n < 0)
      return false;
    id = (id << 4) | unsigned(n);
  }
  return id != 0;
}
bool integerArg(const String &value, unsigned &out, unsigned maximum = 65535) {
  if (!value.length() || value.length() > 10)
    return false;
  out = 0;
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value.c_str()[i];
    if (c < '0' || c > '9' || out > (maximum - unsigned(c - '0')) / 10)
      return false;
    out = out * 10 + unsigned(c - '0');
  }
  return out <= maximum;
}
} // namespace
void DeviceApi::writeHardwareStatus(JsonWriter &j, bool compact, std::string_view view) {
  auto config = [&](const HardwareConfig &c) {
    j << "{\"driveInterface\":" << c.drive << ",\"simulation\":" << unsigned(c.simulation)
      << ",\"bluetoothEnabled\":" << unsigned(c.bluetooth) << ",\"bluetoothMode\":" << c.bluetoothMode
      << ",\"forceApSta\":" << unsigned(c.forceAp) << ",\"buttonGpio\":" << int(c.buttonGpio)
      << ",\"buttonActiveLow\":" << unsigned(c.buttonLow) << ",\"buttonPullup\":" << unsigned(c.buttonPullup)
      << ",\"pwmInverted\":" << unsigned(c.pwmInverted) << '}';
  };
  const auto &r = runtime_.hardwareRecord;
  const auto elapsed = uint32_t(millis()-runtime_.memoryGuard.started);
  const auto &configured = r.phase == ConfigPhase::Testing || r.phase == ConfigPhase::Pending || runtime_.recovery ? r.candidate : r.good;
  const bool ready = runtime_.memoryGuard.ready(millis()) && heapFreeBytes() >= MemoryGuard::HealthyFree &&
      heapMaxBlockBytes() >= MemoryGuard::HealthyBlock &&
      runtimeMetrics.allocationFailures.load() == runtime_.memoryGuard.failures;
  if(compact) {
    j << "{\"token\":" << runtime_.hardwareToken << ",\"generation\":" << r.generation
      << ",\"hasConfirmed\":" << (r.hasConfirmed?"true":"false")
      << ",\"trial\":" << (runtime_.hardwareTrial?"true":"false")
      << ",\"recovery\":" << (runtime_.recovery?"true":"false")
      << ",\"reason\":" << jsonQuoted(recoveryReasonName(runtime_.recoveryReason))
      << ",\"ready\":" << (ready?"true":"false") << ",\"configured\":";
    config(view=="confirmed"?r.good:view=="effective"?runtime_.controller.hardware:configured);
    j << '}';return;
  }
  j << "{\"token\":" << runtime_.hardwareToken << ",\"generation\":" << r.generation
    << ",\"hasConfirmed\":" << (r.hasConfirmed ? "true" : "false")
    << ",\"trial\":" << (runtime_.hardwareTrial ? "true" : "false")
    << ",\"recovery\":" << (runtime_.recovery ? "true" : "false")
    << ",\"reason\":" << jsonQuoted(recoveryReasonName(runtime_.recoveryReason))
    << ",\"ready\":" << (ready ? "true" : "false")
    << ",\"remainingMs\":" << (runtime_.hardwareTrial && elapsed < MemoryGuard::TrialMs ? MemoryGuard::TrialMs-elapsed : 0u)
    << ",\"heapFree\":" << heapFreeBytes() << ",\"heapMaxBlock\":" << heapMaxBlockBytes()
    << ",\"backend\":" << jsonQuoted(runtime_.controller.hardware.backendName())
    << ",\"sppSupported\":" << (SELECTED_BOARD == BOARD_ESP32 && BLUETOOTH_SUPPORT ? "true" : "false")
    << ",\"classicEsp32\":" << (SELECTED_BOARD == BOARD_ESP32 ? "true" : "false")
    << ",\"crowpanel\":" << (SELECTED_BOARD == BOARD_CROWPANEL_21 ? "true" : "false")
    << ",\"crowpanelLocalDrive\":" << (CROWPANEL_LOCAL_DRIVE ? "true" : "false")
    << ",\"separateDrive\":" << (SELECTED_BOARD != BOARD_CROWPANEL_21 && board::output(board::PIN_EL_SPEED) ? "true" : "false")
    << ",\"btsDrive\":" << (CROWPANEL_LOCAL_DRIVE || (SELECTED_BOARD != BOARD_CROWPANEL_21 && board::output(board::PIN_EL_SPEED)) ? "true" : "false")
    << ",\"independentDrive\":" << (SELECTED_BOARD != BOARD_CROWPANEL_21 && board::output(board::PIN_EL_SPEED) ? "true" : "false")
    << ",\"configured\":"; config(configured);
  j << ",\"effective\":"; config(runtime_.controller.hardware);
  j << ",\"confirmed\":"; config(r.good); j << '}';

}
Response DeviceApi::callApi(std::string_view path, FormView form) {
  if(path=="/api/settings") {
    unsigned n=runtime_.loggerSettings.n1mmPort, p=runtime_.loggerSettings.pstPort,
      r=runtime_.loggerSettings.reportPort, command=runtime_.loggerSettings.commandPort;
    for(auto e:form) {
      unsigned *target=e.first=="n1mmPort"?&n:e.first=="pstPort"?&p:e.first=="reportPort"?&r:e.first=="commandPort"?&command:nullptr;
      if(target) {
        uint32_t value=0;
        if(!hardwareUnsigned(e.second,value))return {400,"{\"error\":\"invalid_port\"}"};
        *target=static_cast<unsigned>(value);
      }
    }
    if(!network_.portsValid(runtime_.hud.config.port,n,p,r,command)) return {400,"{\"error\":\"port_conflict\"}"};
  }
  return apiView(runtime_.controller,runtime_.protocol,runtime_.loggerSettings,path,form,millis());
}
void DeviceApi::writeConfiguration(JsonWriter &j) {
  const auto &c=runtime_.controller;const auto &p=runtime_.protocol;const auto &s=runtime_.loggerSettings;
  // Configuration pages exclude ADC samples, ages, counters and motion state:
  // a moving/noisy axis must not invalidate every page of a settings read.
  j << "{\"model\":" << jsonQuoted(c.model==Model::G5500?"G5500":"G800DXA")
    << ",\"rotorName\":" << jsonQuoted(p.rotorName) << ",\"controlMode\":" << jsonQuoted(Controller::modeName(c.mode))
    << ",\"n1mmPort\":" << s.n1mmPort << ",\"pstPort\":" << s.pstPort
    << ",\"reportPort\":" << s.reportPort << ",\"reportHost\":" << jsonQuoted(s.reportHost)
    << ",\"commandPort\":" << s.commandPort
    << ",\"externalAzMode\":" << jsonQuoted(p.externalAzMode==ExternalAzMode::Mechanical?"mechanical":"shortest")
    << ",\"n1mmManualFollow\":" << (p.manualFollow.n1mm?"true":"false")
    << ",\"pstManualFollow\":" << (p.manualFollow.pst?"true":"false")
    << ",\"gs232ManualFollow\":" << (p.manualFollow.gs232?"true":"false")
    << ",\"rotctldManualFollow\":" << (p.manualFollow.rotctld?"true":"false")
    << ",\"azProfile\":" << jsonQuoted(azProfileName(c.azProfile)) << ",\"azStart\":" << c.azRange.start << ",\"azSpan\":" << c.azRange.span()
    << ",\"elProfile\":" << jsonQuoted(elProfileName(c.elProfile)) << ",\"elStart\":" << c.elRange.start << ",\"elSpan\":" << c.elRange.span()
    << ",\"speedEnabled\":" << (c.speedEnabled?"true":"false");
#if SELECTED_BOARD == BOARD_CROWPANEL_21
  j << ",\"encoderInverted\":" << (s.encoderInverted?"true":"false");
#endif
  for(unsigned i=0;i<2;++i) {
    const auto &speed=reportedSpeed(c,i);const char *prefix=i?"el":"az";
    for(auto field : {std::pair<const char *,unsigned>{"SpeedPercent",speed.speedPercent},
        {"StartSpeedPercent",speed.startSpeedPercent},{"ApproachSpeedPercent",speed.approachSpeedPercent},
        {"AccelerationMs",speed.accelerationMs},{"DecelerationMs",speed.decelerationMs},{"SlowdownDegrees",speed.slowdownDegrees}}) {
      j << ",\"" << prefix << field.first << "\":" << field.second;
    }
    j << ",\"" << prefix << "CoastDegrees\":" << speed.coastDegrees
      << ",\"" << prefix << "DecelerationFactor\":" << speed.decelerationFactor;
  }
  j << ",\"speedPercent\":" << reportedSpeed(c,0).speedPercent << ",\"presets\":[";
  for(unsigned i=0;i<4;++i) {
    if(i)j << ',';
    const auto &preset=c.presets[i];
    j << "{\"slot\":" << i+1 << ",\"label\":" << jsonQuoted(preset.label)
      << ",\"target\":{\"az\":" << preset.target.az << ",\"el\":" << preset.target.el
      << ",\"hasAz\":" << (preset.target.hasAz?"true":"false") << ",\"hasEl\":" << (preset.target.hasEl?"true":"false") << "}}";
  }
  j << "],\"build\":{\"board\":" << jsonQuoted(board::NAME) << ",\"displayKind\":" << DISPLAY_KIND
    << ",\"webEnabled\":" << (WEB_INTERFACE_ENABLED?"true":"false")
    << ",\"bluetoothSupported\":" << (BLUETOOTH_SUPPORT?"true":"false") << "}}";
}
void DeviceApi::execute(std::string_view method, std::string_view path, FormView arguments,
                        JsonWriter &output, int &status, const char *&type, bool command) {
  ApiContext web_(arguments,output,status,type);
  auto reply=[&](int code,const char *reason) {
    status=code; type="application/json";
    output << "{\"ok\":" << (code==200?"true":"false") << ",\"error\":" << jsonQuoted(reason) << '}';
  };
  const bool hardwarePath=path=="/api/hardware" || path.substr(0,14)=="/api/hardware/";
  const bool stop=path=="/api/move" && (web_.value("d")=="S" || web_.value("d")=="A" || web_.value("d")=="E");
  if(ota_.locked() && !stop && path!="/api/status" && path!="/api/diaglog" && path!="/api/diagnostics" && path.substr(0,8)!="/api/ota") { reply(503,"update_locked"); return; }
  if(runtime_.recovery && !hardwarePath && path!="/api/status" && path!="/api/network" && path!="/api/diagnostics" && path!="/api/diaglog" && path!="/api/resetreason" && path!="/api/resethistory" && path!="/api/wifi" && path!="/api/ap") { reply(409,"recovery_only"); return; }
  if(hardwarePath) {
    if(method=="GET" && path=="/api/hardware") { writeHardwareStatus(output,command,web_.value("view")); return; }
    if(method!="POST") { reply(405,"method_not_allowed"); return; }
    char encoded[513]; JsonWriter fields(encoded,sizeof encoded);
    for(auto entry:arguments) { if(fields.size())fields << '&'; fields << entry.first << '=' << entry.second; }
    HardwareForm form;
    if(!fields.good() || !parseHardwareForm({encoded,fields.size()},form,path=="/api/hardware")) { reply(400,"invalid_form"); return; }
    if(form.token!=runtime_.hardwareToken || form.generation!=runtime_.hardwareRecord.generation) { reply(409,"stale_configuration"); return; }
    bool saved=false;
    if(path=="/api/hardware/probe") {
      const bool ok=!runtime_.recovery && runtime_.hardwareTrial && runtime_.memoryGuard.ready(millis()) && storage_.probeHardwareStorage(runtime_);
      runtime_.hardwareProbe=ok; reply(ok?200:409,ok?"":"not_ready"); return;
    }
    if(path=="/api/hardware") {
      if(const char *error=board::validateHardware(form.config,runtime_.hardwareHudUart)) { reply(400,error); return; }
      saved=storage_.stageHardware(runtime_,form.config);
    } else if(path=="/api/hardware/confirm") {
      if(runtime_.recovery || !runtime_.hardwareTrial || !runtime_.hardwareProbe || !runtime_.memoryGuard.ready(millis()) || heapFreeBytes()<MemoryGuard::HealthyFree || heapMaxBlockBytes()<MemoryGuard::HealthyBlock || runtimeMetrics.allocationFailures.load()!=runtime_.memoryGuard.failures) { reply(409,"not_ready"); return; }
      saved=storage_.confirmHardware(runtime_);
    } else if(path=="/api/hardware/rollback") {
      if(!runtime_.hardwareRecord.hasConfirmed) { reply(409,"no_confirmed_configuration"); return; }
      auto good=runtime_.hardwareRecord.good;
      if(!BLUETOOTH_SUPPORT)good.bluetooth=false;
      saved=storage_.stageHardware(runtime_,good);
    } else if(path=="/api/hardware/recovery") saved=storage_.enterRecovery(runtime_,RecoveryReason::Requested);
    else { reply(404,"not_found"); return; }
    if(!saved) { reply(503,"storage_failed"); return; }
    runtime_.controller.stopNow(-1,millis()); hardware_.applyOutputs(); runtime_.controller.configurationLocked=true;
    reply(200,""); restart_.request(millis()); return;
  }
  if(path=="/api/ota" && method=="GET") {
    char data[768];const auto n=ota_.status(data,sizeof data);output << std::string_view(data,n);return;
  }
  if(path=="/api/ota/arduino" && method=="POST") {
    if(!ota_.authorized(web_.value("token"))) {reply(403,"ota_token");return;}
    const auto error=ota_.armArduino();reply(error?409:200,error?error:"");return;
  }
  if(path=="/api/ota/reboot" && method=="POST") {
    if(!ota_.authorized(web_.value("token"))) {reply(403,"ota_token");return;}
    if(!ota_.locked()) {reply(409,"not_locked");return;}
    ota_.reboot();reply(200,"");return;
  }
  if(path=="/api/motion/autotune" && method=="GET" && command && web_.value("full")!="1") {
    if(runtime_.hud.isHud()) {reply(409,"controller_settings_only");return;}
    writeAutoTune(output,runtime_.controller);return;
  }
  if(path=="/api/status" && command) {
    auto &c=runtime_.controller;
    if(!runtime_.recovery)actions_.updatePosition();
    if(web_.value("view")=="axis") {
      const auto axis=web_.value("axis");
      if(axis!="az" && axis!="el") {reply(400,"invalid_axis");return;}
      const unsigned i=axis=="az"?0:1;const auto &a=c.axes[i];
      output << "{\"current\":" << a.current << ",\"target\":" << a.target
        << ",\"tracking\":" << (a.tracking?"true":"false")
        << ",\"completed\":" << (a.completed?"true":"false")
        << ",\"direction\":" << a.direction << ",\"stopToleranceDegrees\":" << c.effectiveTolerance(i)
        << ",\"speedOutputPercent\":" << a.speedOutputPercent;
      writeAxisMotion(output,c,i);output << '}';return;
    }
    output << "{\"model\":" << jsonQuoted(c.model==Model::G5500?"G5500":"G800DXA")
      << ",\"role\":" << jsonQuoted(runtime_.hud.isHud()?"hud":"controller")
      << ",\"controlMode\":" << jsonQuoted(Controller::modeName(c.mode))
      << ",\"remainingMs\":" << c.remaining(millis())
      << ",\"moving\":" << (c.moving()?"true":"false") << ",\"pendingValid\":" << (c.pendingValid?"true":"false")
      << ",\"pending\":{\"az\":" << c.pending.az << ",\"el\":" << c.pending.el
      << ",\"hasAz\":" << (c.pending.hasAz?"true":"false") << ",\"hasEl\":" << (c.pending.hasEl?"true":"false") << '}'
      << ",\"presetActive\":" << (c.presetActive?"true":"false") << ",\"presetSlot\":" << c.presetSlot
      << ",\"fault\":" << jsonQuoted(c.fault) << ",\"recovery\":" << (runtime_.recovery?"true":"false")
      << ",\"reason\":" << jsonQuoted(recoveryReasonName(runtime_.recoveryReason))
      << ",\"configurationLocked\":" << (c.configurationLocked?"true":"false")
      << ",\"commandPort\":" << runtime_.loggerSettings.commandPort << ",\"axes\":[";
    for(unsigned i=0;i<2;++i) {
      if(i)output << ',';
      const auto &a=c.axes[i];
      output << "{\"current\":" << a.current << ",\"target\":" << a.target
        << ",\"calibrated\":" << (a.calibrated?"true":"false") << ",\"output\":" << a.speedOutputPercent << '}';
    }
    output << "]}";return;
  }
  if(path=="/api/diagnostics" && method=="GET") {
    if(web_.hasArg("failure")) {
      unsigned i; if(!integerArg(web_.arg("failure"),i,1)) {reply(400,"invalid_failure");return;}
      const auto &f=runtimeMetrics.failures[i];
      output << "{\"ready\":" << (f.ready()?"true":"false");
      if(f.ready()) {
        output << ",\"size\":" << f.size << ",\"caps\":" << f.caps << ",\"free\":" << f.freeBytes
          << ",\"largestBlock\":" << f.largestBlock << ",\"function\":" << jsonQuoted(f.function)
          << ",\"task\":" << jsonQuoted(f.task) << ",\"pc\":[";
        for(unsigned n=0;n<8;++n) {if(n)output << ',';output << f.pc[n];} output << ']';
      }
      output << '}';return;
    }
    output << "{\"heapFree\":" << heapFreeBytes() << ",\"heapMin\":" << heapMinBytes()
      << ",\"heapMaxBlock\":" << heapMaxBlockBytes() << ",\"allocFailures\":" << runtimeMetrics.allocationFailures.load()
      << ",\"maxFailedAllocation\":" << runtimeMetrics.largestFailedAllocation.load()
      << ",\"loopMaxUs\":" << runtimeMetrics.loopMaxUs << ",\"loopsOver20ms\":" << runtimeMetrics.loopsOver20ms
      << ",\"webEnabled\":" << (WEB_INTERFACE_ENABLED?"true":"false") << ",\"bluetoothSupported\":" << (BLUETOOTH_SUPPORT?"true":"false")
      << ",\"psramBytes\":" << heap_caps_get_total_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT) << '}';return;
  }
  if(path=="/api/network" && method=="GET") {
    const bool connected=WiFi.status()==WL_CONNECTED;
    const String sta=WiFi.localIP().toString(),ap=WiFi.softAPIP().toString();
    output << "{\"wifiMode\":" << unsigned(WiFi.getMode()) << ",\"connected\":" << (connected?"true":"false")
      << ",\"ssid\":" << jsonQuoted(network_.credentials().wifiSsid.c_str())
      << ",\"staIp\":" << jsonQuoted(sta.c_str()) << ",\"apIp\":" << jsonQuoted(ap.c_str())
      << ",\"apAlwaysOn\":" << (network_.apAlwaysOn()?"true":"false") << ",\"rssi\":";
    if(connected)output << WiFi.RSSI();else output << "null";
    output << ",\"n1mmPort\":" << runtime_.loggerSettings.n1mmPort << ",\"pstPort\":" << runtime_.loggerSettings.pstPort
      << ",\"hudPort\":" << runtime_.hud.config.port << ",\"commandPort\":" << runtime_.loggerSettings.commandPort << '}';return;
  }
  if(path=="/api/config" && method=="GET") {writeConfiguration(output);return;}
  if(path=="/api/diaglog" && method=="GET") {
    diagnostics_.writeLog(output); web_.finish(200,"text/plain; charset=utf-8");

    return;
  }
  if(path=="/api/diaglog/clear" && (method=="GET" || method=="POST")) {
    diagnostics_.diagClear();
    web_.send(200, "text/plain", "OK");

    return;
  }
  if(path=="/api/adcdiag" && method=="GET") {
    if (runtime_.hud.isHud()) {
      web_.send(403, "application/json", "{\"error\":\"controller_settings_only\"}");
      return;
    }
    int azRaw = 0, azMv = 0, elRaw = 0, elMv = 0;
    hardware_.adcDiagRead(PIN_AZ_ADC, azRaw, azMv);
    if (runtime_.controller.model == forkrot::Model::G5500) {
      hardware_.adcDiagRead(PIN_EL_ADC, elRaw, elMv);
    }
    String j = String("{\"hasEl\":") +
               (runtime_.controller.model == forkrot::Model::G5500 ? "true" : "false") +
               ",\"azRaw\":" + String(azRaw) + ",\"azMv\":" + String(azMv) +
               ",\"elRaw\":" + String(elRaw) + ",\"elMv\":" + String(elMv) + "}";
    web_.send(200, "application/json", j);

    return;
  }
  if(path=="/api/move" && method=="GET") {
    auto r = callApi("/api/move",arguments);
    runtime_.controller.tick(millis());
    hardware_.applyOutputs();
    web_.send(r.status, "application/json", r.body.c_str());

    return;
  }
  if(path=="/api/target" && method=="GET") {
    actions_.updatePosition();
    auto response = callApi("/api/target",arguments);
    runtime_.controller.tick(millis());
    hardware_.applyOutputs();
    runtime_.targetValid = runtime_.controller.targetKnown;
    web_.send(response.status, "application/json", response.body.c_str());

    return;
  }
  if(path=="/api/azrange" && method=="POST") {
    if (runtime_.hud.isHud()) { web_.send(403,"application/json","{\"error\":\"controller_settings_only\"}"); return; }
    const String profile = web_.arg("profile");
    const uint8_t mode = profile == "p36" ? AZ_P36 : profile == "p45" ? AZ_P45 : AZ_CUSTOM;
    float start=0, span=mode == AZ_P36 ? 360.f : 450.f;
    const bool custom = profile == "custom";
    if ((!custom && profile != "p36" && profile != "p45") ||
        (custom && (!number(web_.arg("start").c_str(),start) || !number(web_.arg("span").c_str(),span))) ||
        !AzRange::tenth(span) || !validAzProfile(mode,{start,start+span})) {
      web_.send(400,"application/json","{\"error\":\"invalid_range\"}"); return;
    }
    const bool ok=actions_.configureAzRange(mode,start,span);
    web_.send(ok?200:503,"application/json",ok?"{\"ok\":true}":"{\"error\":\"calibration_save_failed\"}");

    return;
  }
  if(path=="/api/azcal" && method=="GET") {
    if (runtime_.hud.isHud()) { web_.send(403,"application/json","{\"error\":\"controller_settings_only\"}"); return; }
    auto &j=output;
    calibration_.writeAzJson(j); web_.finish(j.good()?200:503,"application/json");

    return;
  }
  if(path=="/api/azcal" && method=="POST") {
    if (runtime_.hud.isHud()) { web_.send(403,"application/json","{\"error\":\"controller_settings_only\"}"); return; }
    float angle; const String op=web_.arg("op");
    Calibration candidate(runtime_.controller); candidate.state()=calibration_.state();
    if (!number(web_.arg("angle").c_str(),angle) || !candidate.editAzPoint(op.c_str(),angle,0)) {
      web_.send(400,"application/json","{\"error\":\"invalid_point\"}"); return;
    }
    const bool ok=actions_.editAzPoint(op.c_str(),angle);
    web_.send(ok?200:503,"application/json",ok?"{\"ok\":true}":"{\"error\":\"calibration_save_failed\"}");

    return;
  }
  if(path=="/api/elrange" && method=="POST") {
    if (runtime_.hud.isHud() || runtime_.controller.model != Model::G5500) { web_.send(403,"application/json","{\"error\":\"controller_settings_only\"}"); return; }
    const String profile = web_.arg("profile");
    const uint8_t mode = profile == "standard" ? EL_STANDARD : EL_CUSTOM;
    float start=0, span=180.f;
    const bool custom = profile == "custom";
    if ((!custom && profile != "standard") ||
        (custom && (!number(web_.arg("start").c_str(),start) || !number(web_.arg("span").c_str(),span))) ||
        !AzRange::tenth(span) || !validElProfile(mode,{start,start+span})) {
      web_.send(400,"application/json","{\"error\":\"invalid_range\"}"); return;
    }
    const bool ok=actions_.configureElRange(mode,start,span);
    web_.send(ok?200:503,"application/json",ok?"{\"ok\":true}":"{\"error\":\"calibration_save_failed\"}");

    return;
  }
  if(path=="/api/elcal" && method=="GET") {
    if (runtime_.hud.isHud() || runtime_.controller.model != Model::G5500) { web_.send(403,"application/json","{\"error\":\"controller_settings_only\"}"); return; }
    auto &j=output;
    calibration_.writeElJson(j); web_.finish(j.good()?200:503,"application/json");

    return;
  }
  if(path=="/api/elcal" && method=="POST") {
    if (runtime_.hud.isHud() || runtime_.controller.model != Model::G5500) { web_.send(403,"application/json","{\"error\":\"controller_settings_only\"}"); return; }
    float angle; const String op=web_.arg("op");
    Calibration candidate(runtime_.controller); candidate.state()=calibration_.state();
    if (!number(web_.arg("angle").c_str(),angle) || !candidate.editElPoint(op.c_str(),angle,0)) {
      web_.send(400,"application/json","{\"error\":\"invalid_point\"}"); return;
    }
    const bool ok=actions_.editElPoint(op.c_str(),angle);
    web_.send(ok?200:503,"application/json",ok?"{\"ok\":true}":"{\"error\":\"calibration_save_failed\"}");

    return;
  }
  if(path=="/api/cal" && (method=="GET" || method=="POST")) {
    if (runtime_.hud.isHud()) {
      web_.send(403, "application/json", "{\"error\":\"controller_settings_only\"}");
      return;
    }
    auto &calibrationState = calibration_.state();
    // MULTI-POINT. AZ is written only to the currently selected P36/P45 table.
    String p = web_.arg("p");
    if (runtime_.controller.model == forkrot::Model::G800DXA && p.startsWith("el")) {
      web_.send(400, "text/plain", "NO EL AXIS");
      return;
    }
    int a = hardware_.adcAvgMv(PIN_AZ_ADC), e = hardware_.adcAvgMv(PIN_EL_ADC);
    if (p.startsWith("el") && calibrationState.elProfile == EL_CUSTOM) { web_.send(400,"text/plain","USE /api/elcal"); return; }
    if (p.startsWith("az")) {
      if (calibrationState.gsAzMode == AZ_CUSTOM) { web_.send(400,"text/plain","USE /api/azcal"); return; }
      int *tbl = (calibrationState.gsAzMode == 36) ? calibrationState.az36CalMv
                                                   : calibrationState.az45CalMv;
      uint8_t n = (calibrationState.gsAzMode == 36) ? AZ36_CAL_N : AZ45_CAL_N;
      int idx = -1;
      if (p == "az0") {
        idx = 0;
      } else if (p == "az90") {
        idx = 1;
      } else if (p == "az180") {
        idx = 2;
      } else if (p == "az270") {
        idx = 3;
      } else if (p == "az360") {
        idx = 4;
      } else if (p == "az450") {
        idx = 5;
      }
      if (idx < 0 || idx >= n) {
        web_.send(400, "text/plain", "POINT NOT IN ACTIVE AZ RANGE");
        return;
      }
      tbl[idx] = a;
    } else if (p == "el0") {
      calibrationState.elCalMv[0] = e;
    } else if (p == "el45") {
      calibrationState.elCalMv[1] = e;
    } else if (p == "el90") {
      calibrationState.elCalMv[2] = e;
    } else if (p == "el135") {
      calibrationState.elCalMv[3] = e;
    } else if (p == "el180") {
      calibrationState.elCalMv[4] = e;
    } else {
      web_.send(400, "text/plain", "BAD POINT");
      return;
    }
    const bool saved = actions_.saveCal();
    web_.send(saved ? 200 : 503, "text/plain", saved ? "MULTI SAVED" : "CALIBRATION SAVE FAILED");

    return;
  }
  if(path=="/api/cal2" && (method=="GET" || method=="POST")) {
    if (runtime_.hud.isHud()) {
      web_.send(403, "application/json", "{\"error\":\"controller_settings_only\"}");
      return;
    }
    String p = web_.arg("p");
    p.toUpperCase();
    if (runtime_.controller.model == forkrot::Model::G800DXA && (p == "O2" || p == "F2")) {
      web_.send(400, "text/plain", "NO EL AXIS");
      return;
    }
    if (p == "O" || p == "O2" || p == "F" || p == "F2") {
      actions_.captureGs232Cal(p);
      const bool saved = runtime_.controller.fault != "calibration_save_failed";
      web_.send(saved ? 200 : 503, "text/plain", saved ? "2-POINT SAVED" : "CALIBRATION SAVE FAILED");
    } else {
      web_.send(400, "text/plain", "BAD COMMAND");
    }

    return;
  }
  if(path=="/api/calmode" && (method=="GET" || method=="POST")) {
    if (runtime_.hud.isHud()) {
      web_.send(403, "application/json", "{\"error\":\"controller_settings_only\"}");
      return;
    }
    String m = web_.arg("m");
    m.toLowerCase();
    if (m == "2" || m == "gs232" || m == "2point") {
      actions_.setCalibrationMode(CAL_GS232_2POINT);
    } else if (m == "multi") {
      actions_.setCalibrationMode(CAL_MULTI_POINT);
    } else {
      web_.send(400, "text/plain", "BAD MODE");
      return;
    }
    calibration_.state().cal.valid = calibration_.selectedCalibrationValid();
    const bool saved = runtime_.controller.fault != "calibration_save_failed";
    web_.send(saved ? 200 : 503, "text/plain", saved ? calibration_.calibrationModeName() : "CALIBRATION SAVE FAILED");

    return;
  }
  if(path=="/api/azmode" && (method=="GET" || method=="POST")) {
    if (runtime_.hud.isHud()) {
      web_.send(403, "application/json", "{\"error\":\"controller_settings_only\"}");
      return;
    }
    int m = web_.arg("m").toInt();
    if (m != 36 && m != 45) {
      web_.send(400, "text/plain", "BAD AZ MODE");
      return;
    }
    const bool saved = actions_.configureAzRange(uint8_t(m), 0, m == 36 ? 360 : 450);
    web_.send(saved ? 200 : 503, "text/plain", saved ? (m == 36 ? "P36" : "P45") : "CALIBRATION SAVE FAILED");

    return;
  }
  if(path=="/api/calreset" && (method=="GET" || method=="POST")) {
    if (runtime_.hud.isHud()) {
      web_.send(403, "application/json", "{\"error\":\"controller_settings_only\"}");
      return;
    }
    const String axis = web_.arg("axis"), mode = web_.arg("mode");
    if ((axis != "az" && axis != "el") || (mode != "2" && mode != "multi") ||
        (axis == "el" && runtime_.controller.model != Model::G5500)) {
      web_.send(400,"text/plain","BAD AXIS OR MODE"); return;
    }
    if(axis == "az") calibration_.resetAz(mode == "multi");
    else calibration_.resetEl(mode == "multi");
    const bool saved=actions_.saveCal();
    web_.send(saved?200:503,"text/plain",saved?"RESET OK":"CALIBRATION SAVE FAILED");

    return;
  }
  if(path=="/api/ap" && (method=="GET" || method=="POST")) {
    String p = web_.arg("pass");
    if (p.length() != 0 && (p.length() < 8 || p.length() > 63)) {
      web_.send(400, "text/plain", "AP password must be empty or 8..63 characters");
      return;
    }
    storage_.saveApPassword(p);
    web_.send(200, "text/plain", p.length() ? "AP password saved" : "AP set to open");
    restart_.request(millis());

    return;
  }
  if(path=="/api/resetreason" && method=="GET") {
    String j = "{\"reason\":\"" + diagnostics_.bootReasonText() +
               "\",\"code\":" + String(diagnostics_.bootReasonCode()) + "}";
    web_.send(200, "application/json", j);

    return;
  }
  if(path=="/api/resethistory" && method=="GET") {
            auto &j=output;
            storage_.writeResetHistoryJson(j);
            web_.finish(j.good()?200:503,"application/json");

    return;
  }
  if(path=="/api/resethistory/clear" && (method=="GET" || method=="POST")) {
    storage_.clearResetHistory();
    web_.send(200, "text/plain", "OK");

    return;
  }
  if(path=="/api/rssi" && method=="GET") {
    const String range = web_.arg("range");
    const auto period = range == "day" ? RssiPeriod::Day : range == "week" ? RssiPeriod::Week :
                        range == "month" ? RssiPeriod::Month : range == "year" ? RssiPeriod::Year : RssiPeriod::Hour;
    auto &j=output;
    diagnostics_.writeHistoryJson(j, period);
    web_.finish(j.good()?200:503,"application/json");

    return;
  }
  if(path=="/api/status" && method=="GET") {
    web_.sendHeader("Cache-Control", "no-store");
    auto &calibrationState = calibration_.state();
    actions_.updatePosition();
    bool staOk = (WiFi.status() == WL_CONNECTED);
    String staIp = staOk ? WiFi.localIP().toString() : "-";
    const bool apEnabled = (WiFi.getMode() & WIFI_AP) != 0;
    String apIp = apEnabled ? WiFi.softAPIP().toString() : "-";
    String ip = staOk ? staIp : apIp;
    const char *mode = staOk ? (apEnabled ? "AP + Wi-Fi Client" : "Wi-Fi Client") : apEnabled ? "Access Point" : "Disconnected";
    int rssi = staOk ? WiFi.RSSI() : 0;
    String signal = !staOk
                        ? "-"
                        : (rssi >= -55 ? "Excellent"
                                       : (rssi >= -67 ? "Good" : (rssi >= -75 ? "Fair" : "Weak")));
    String rssiJson = staOk ? String(rssi) : "null";
    uint32_t targetAge = runtime_.targetValid ? (millis() - runtime_.lastTargetRxMs) : 0;
    const int *azTbl =
        (calibrationState.gsAzMode == 36) ? calibrationState.az36CalMv : calibrationState.az45CalMv;
    int azN = (calibrationState.gsAzMode == 36) ? AZ36_CAL_N : AZ45_CAL_N;
    if (runtime_.hud.isHud())
      calibrationState.gsAzMode = runtime_.controller.azProfile;
    bool azLin =
        runtime_.hud.isHud() ? runtime_.controller.axes[0].calibrated : calibration_.activeAzLinearValid();
    bool azMul = calibration_.activeAzMultiValid();
    int az450v = (azN > 5) ? azTbl[5] : -1;
    int az2z = calibration_.activeAzOSet() ? calibration_.activeAz2Zero() : -1;
    int az2f = calibration_.activeAzFSet() ? calibration_.activeAz2Full() : -1;
    calibrationState.cal.valid = calibration_.selectedCalibrationValid();

    char btMac[18];
    network_.bluetoothMac(btMac);
    auto &j=output;
    j << '{'; actions_.writeWebDiagnostics(j);
    j << ",\"az\":" << runtime_.currentAz << ",\"el\":" << runtime_.currentEl
      << ",\"targetAz\":" << runtime_.targetAz << ",\"targetEl\":" << runtime_.targetEl
      << ",\"targetValid\":" << (runtime_.targetValid ? "true" : "false") << ",\"targetAgeMs\":" << targetAge
      << ",\"calibrated\":" << (calibrationState.cal.valid ? "true" : "false") << ",\"selectedCalOk\":"
      << ((runtime_.hud.isHud()
               ? runtime_.controller.axes[0].calibrated &&
                     (runtime_.controller.model == Model::G800DXA || runtime_.controller.axes[1].calibrated)
               : calibration_.selectedCalibrationValid())
              ? "true"
              : "false")
      << ",\"azMode\":" << calibrationState.gsAzMode << ",\"azLinear\":" << (azLin ? "true" : "false")
      << ",\"azLinear36\":" << (calibrationState.az36LinearValid ? "true" : "false")
      << ",\"azLinear45\":" << (calibrationState.az45LinearValid ? "true" : "false")
      << ",\"elLinear\":" << (calibration_.activeElLinearValid() ? "true" : "false")
      << ",\"azMulti\":" << (azMul ? "true" : "false")
      << ",\"azMulti36\":" << (calibrationState.az36MultiValid ? "true" : "false")
      << ",\"azMulti45\":" << (calibrationState.az45MultiValid ? "true" : "false")
      << ",\"elMulti\":" << (calibration_.activeElMultiValid() ? "true" : "false")
      << ",\"calMode\":" << jsonQuoted(calibrationState.calibrationMode == CAL_MULTI_POINT ? "multi" : "2")
      << ",\"az2p0\":" << az2z << ",\"az2pf\":" << az2f
      << ",\"el2p0\":" << (calibration_.activeElOSet() ? calibration_.activeEl2Zero() : -1)
      << ",\"el2pf\":" << (calibration_.activeElFSet() ? calibration_.activeEl2Full() : -1)
      << ",\"motion\":" << jsonQuoted(hardware_.motion().c_str()) << ",\"azp0\":" << azTbl[0]
      << ",\"azp90\":" << azTbl[1] << ",\"azp180\":" << azTbl[2] << ",\"azp270\":" << azTbl[3]
      << ",\"azp360\":" << azTbl[4] << ",\"azp450\":" << az450v << ",\"elp0\":" << calibrationState.elCalMv[0]
      << ",\"elp45\":" << calibrationState.elCalMv[1] << ",\"elp90\":" << calibrationState.elCalMv[2]
      << ",\"elp135\":" << calibrationState.elCalMv[3] << ",\"elp180\":" << calibrationState.elCalMv[4]
      << ",\"ip\":" << jsonQuoted(ip.c_str()) << ",\"apIp\":" << jsonQuoted(apIp.c_str())
      << ",\"staIp\":" << jsonQuoted(staIp.c_str()) << ",\"wifiMode\":" << jsonQuoted(mode)
      << ",\"ssid\":" << jsonQuoted(network_.credentials().wifiSsid.c_str())
      << ",\"rssi\":" << rssiJson.c_str() << ",\"signal\":" << jsonQuoted(signal.c_str())
      << ",\"btConnected\":" << (network_.bluetoothConnected() ? "true" : "false")
      << ",\"btLinkConnected\":" << (network_.bluetoothLinkConnected() ? "true" : "false")
      << ",\"btMac\":" << jsonQuoted(btMac)
      << ",\"resetReason\":" << jsonQuoted(diagnostics_.bootReasonText().c_str())
      << ",\"uptimeSeconds\":" << uint32_t(esp_timer_get_time() / 1000000)
      << ",\"heapFree\":" << heapFreeBytes() << ",\"heapMinFree\":" << heapMinBytes()
      << ",\"heapMaxBlock\":" << heapMaxBlockBytes()
      << ",\"httpWriteFailures\":" << network_.httpDiagnostics().writeFailures;
    j << ",\"httpReadFailures\":" << network_.httpDiagnostics().readFailures << ",\"httpRejected\":" << network_.httpDiagnostics().rejected
      << ",\"httpAccepted\":" << network_.httpDiagnostics().accepted << ",\"httpRequests\":" << network_.httpDiagnostics().requests;
    const auto tcp = tcpMemory();
    j << ",\"tcpStatsAvailable\":" << (tcp.available ? "true" : "false")
      << ",\"tcpActive\":" << tcp.active << ",\"tcpClosing\":" << tcp.closing << ",\"tcpTimeWait\":" << tcp.timewait;
    j << ",\"allocFailures\":" << runtimeMetrics.allocationFailures.load()
      << ",\"maxFailedAllocation\":" << runtimeMetrics.largestFailedAllocation.load()
      << ",\"loopMaxUs\":" << runtimeMetrics.loopMaxUs << ",\"loopsOver20ms\":" << runtimeMetrics.loopsOver20ms;
    runtime_.hud.writeStatus(j);
    if (!writeStateJson(j, runtime_.controller, runtime_.protocol, runtime_.loggerSettings, millis(), true)) {
      web_.send(503, "application/json", "{\"error\":\"status_too_large\"}");
      return;
    }
    web_.finish(200,"application/json");

    return;
  }
  if(path=="/api/move" && method=="POST") {
    auto r = callApi("/api/move",arguments);
    runtime_.controller.tick(millis());
    hardware_.applyOutputs();
    web_.send(r.status, "application/json", r.body.c_str());

    return;
  }
  if(path=="/api/target" && method=="POST") {
    actions_.updatePosition();
    auto r = callApi("/api/target",arguments);
    runtime_.controller.tick(millis());
    hardware_.applyOutputs();
    if (r.status == 200) {
      runtime_.targetValid = runtime_.controller.targetKnown;
    }
    web_.send(r.status, "application/json", r.body.c_str());

    return;
  }
  if(path=="/api/motion" && method=="GET") {
    if(runtime_.hud.isHud()) { web_.send(409,"application/json","{\"ok\":false,\"result\":\"controller_settings_only\"}"); return; }
    auto &j=output;
    writeMotionConfiguration(j,runtime_.controller); web_.finish(j.good()?200:503,"application/json");

    return;
  }
  if(path=="/api/motion/autotune" && method=="GET") {
    if(runtime_.hud.isHud()) { web_.send(409,"application/json","{\"ok\":false,\"result\":\"controller_settings_only\"}"); return; }
    auto &j=output;writeAutoTune(j,runtime_.controller,true);web_.finish(j.good()?200:503,"application/json");

    return;
  }
  if(path=="/api/device" && method=="GET") {
    auto &j=output;
    j << "{\"ok\":true";
    runtime_.hud.writeStatus(j);
    j << '}';
    web_.finish(j.good()?200:503,"application/json");

    return;
  }
  if(path=="/api/device" && method=="POST") {
    auto config = runtime_.hud.config;
    const String role = web_.arg("role"), wifi = web_.arg("wifi"), uart = web_.arg("uart");
    unsigned port = 0;
    if ((role != "hud" && role != "controller") ||
        (wifi != "existing" && wifi != "host" && wifi != "client") || (uart != "0" && uart != "1") ||
        !integerArg(web_.arg("port"), port) ||
        !network_.hudPortValid(port, runtime_.loggerSettings.n1mmPort, runtime_.loggerSettings.pstPort,
                               runtime_.loggerSettings.reportPort)) {
      web_.send(400, "application/json", "{\"ok\":false,\"result\":\"invalid_or_port_conflict\"}");
      return;
    }
    if (uart == "1" && (board::PIN_UART_RX < 0 || board::PIN_UART_TX < 0 ||
        (SELECTED_BOARD == BOARD_CROWPANEL_21 && runtime_.controller.hardware.backend() == BT_HC05))) {
      web_.send(400, "application/json", "{\"ok\":false,\"result\":\"uart_in_use_by_hc05\"}");
      return;
    }
    config.role = role == "hud" ? DeviceRole::Hud : DeviceRole::Controller;
    config.wifi = wifi == "host" ? HudWifi::Host : wifi == "client" ? HudWifi::Client : HudWifi::Existing;
    config.uart = uart == "1";
    config.port = uint16_t(port);
    if (!storage_.saveHudConfig(config)) {
      web_.send(507, "application/json", "{\"ok\":false,\"result\":\"storage_full\"}");
      return;
    }
    // Stop local outputs before rebooting into another role. Do not alter remote numerical jobs.
    if (!runtime_.hud.isHud()) {
      runtime_.controller.stopNow(-1, millis());
      hardware_.applyOutputs();
      runtime_.controller.driveFault = true;
    }
    web_.send(200, "application/json", "{\"ok\":true,\"result\":\"restart\"}");
    restart_.request(millis());

    return;
  }
  if(path=="/api/hud/peers" && method=="GET") {
    unsigned offset = 0, limit = command ? 1 : 8;
    if ((web_.hasArg("offset") && !integerArg(web_.arg("offset"), offset, 1000000000)) ||
        (web_.hasArg("limit") && (!integerArg(web_.arg("limit"), limit, 8) || !limit))) {
      web_.send(400, "application/json", "{\"error\":\"invalid_page\"}");
      return;
    }
    auto &j=output;
    runtime_.hud.writePeers(j, offset, limit, web_.arg("discovered") == "1");
    web_.finish(j.good()?200:503,"application/json");

    return;
  }
  if(path=="/api/hud/discover" && method=="POST") {
    auto &hud = runtime_.hud;
    if (!hud.isHud()) {
      web_.send(409, "application/json", "{\"ok\":false,\"result\":\"hud_only\"}");
      return;
    }
    HudEndpoint endpoint;
    endpoint.port = hud.config.port;
    endpoint.uart = web_.arg("transport") == "uart";
    if (endpoint.uart && !hud.config.uart) {
      web_.send(400, "application/json", "{\"ok\":false,\"result\":\"uart_disabled\"}");
      return;
    }
    if (!endpoint.uart) {
      IPAddress ip;
      const String address = web_.arg("address");
      if (address.length()) {
        if (!ip.fromString(address) || ip.type() != IPv4 || uint32_t(ip) == 0) {
          web_.send(400, "application/json", "{\"ok\":false,\"result\":\"invalid_address\"}");
          return;
        }
        endpoint.address = uint32_t(ip);
      } else
        endpoint.address = 0xffffffffu;
    }
    unsigned port = endpoint.port;
    if (web_.hasArg("port") && (!integerArg(web_.arg("port"), port) || !port)) {
      web_.send(400, "application/json", "{\"ok\":false,\"result\":\"invalid_port\"}");
      return;
    }
    endpoint.port = uint16_t(port);
    const bool ok = hud.discover(endpoint);
    web_.send(ok ? 200 : 503, "application/json",
              ok ? "{\"ok\":true,\"result\":\"searching\"}" : "{\"ok\":false,\"result\":\"unavailable\"}");

    return;
  }
  if(path=="/api/hud/peers" && method=="POST") {
    auto &hud = runtime_.hud;
    uint64_t id = 0;
    if (!hud.isHud() || !nodeId(web_.arg("id"), id)) {
      web_.send(400, "application/json", "{\"ok\":false,\"result\":\"invalid\"}");
      return;
    }
    auto *p = hud.peer(id);
    if (!p) {
      web_.send(404, "application/json", "{\"ok\":false,\"result\":\"discover_first\"}");
      return;
    }
    const bool oldSaved = p->saved;
    char oldLabel[49];
    std::memcpy(oldLabel, p->label, sizeof oldLabel);
    if (web_.arg("action") == "remove")
      p->saved = false;
    else {
      const auto label = web_.arg("name");
      if (!hud.add(id, p->endpoint, label.length() ? label.c_str() : p->state.name, true)) {
        web_.send(400, "application/json", "{\"ok\":false,\"result\":\"invalid_name\"}");
        return;
      }
    }
    if (!storage_.saveHudPeers(hud)) {
      p->saved = oldSaved;
      std::memcpy(p->label, oldLabel, sizeof oldLabel);
      web_.send(507, "application/json", "{\"ok\":false,\"result\":\"storage_full\"}");
      return;
    }
    bool selectionSaved = true;
    if (!p->saved) {
      hud.remove(id);
      selectionSaved = storage_.saveHudConfig(hud.config);
    } else if (!hud.config.selected) {
      hud.select(id);
      selectionSaved = storage_.saveHudConfig(hud.config);
    }
    if (!selectionSaved) {
      hud.catalogError = true;
      web_.send(507, "application/json", "{\"ok\":false,\"result\":\"storage_full\"}");
      return;
    }
    web_.send(200, "application/json", "{\"ok\":true,\"result\":\"ok\"}");

    return;
  }
  if(path=="/api/hud/select" && method=="POST") {
    auto &hud = runtime_.hud;
    uint64_t id = 0;
    const auto old = hud.config.selected;
    if (!hud.isHud() || !nodeId(web_.arg("id"), id) || !hud.select(id)) {
      web_.send(400, "application/json", "{\"ok\":false,\"result\":\"invalid_peer\"}");
      return;
    }
    if (!storage_.saveHudConfig(hud.config)) {
      hud.select(old);
      web_.send(507, "application/json", "{\"ok\":false,\"result\":\"storage_full\"}");
      return;
    }
    actions_.invalidateDisplay();
    web_.send(200, "application/json", "{\"ok\":true,\"result\":\"ok\"}");

    return;
  }
  if(path=="/api/hud/command" && method=="GET") {
    unsigned request = 0;
    if (!integerArg(web_.arg("id"), request, UINT32_MAX) || !request) {
      web_.send(400, "application/json", "{\"error\":\"invalid_id\"}");
      return;
    }
    const auto reply = runtime_.hud.result(request);
    auto &j=output;
    j << "{\"requestId\":" << request
      << ",\"ok\":" << ((reply.result == Result::Ok || reply.result == Result::Pending) ? "true" : "false")
      << ",\"result\":" << jsonQuoted(Controller::resultName(reply.result)) << '}';
    web_.finish(reply.result == Result::Deferred ? 202 : 200,"application/json");

    return;
  }
  if(path=="/api/wifi" && method=="GET") {
    const String ssid = web_.arg("ssid"), password = web_.arg("pass");
    const auto result = storage_.saveWifi(ssid, password);
    if (result != WifiSaveResult::Ok) {
      diagnostics_.dbgPrintln(String("[WIFI NVS] save failed, stage=") +
                             (result == WifiSaveResult::InvalidSsid ? "invalid_ssid" :
                              result == WifiSaveResult::InvalidPassword ? "invalid_password" :
                              result == WifiSaveResult::OpenFailed ? "open" :
                              result == WifiSaveResult::WriteFailed ? "write" : "verify"));
      const bool invalid = result == WifiSaveResult::InvalidSsid || result == WifiSaveResult::InvalidPassword;
      const char *message = result == WifiSaveResult::InvalidSsid ? "SSID должен содержать от 1 до 32 байт." :
                            result == WifiSaveResult::InvalidPassword ? "Пароль должен содержать 8–63 символа, 64 шестнадцатеричные цифры или быть пустым для открытой сети." :
                            "Не удалось сохранить настройки Wi-Fi. Перезагрузка отменена.";
      web_.send(invalid ? 400 : 500, "text/plain; charset=utf-8", message);
      return;
    }
    diagnostics_.dbgPrintln("[WIFI NVS] credentials saved and verified; restarting");
    web_.send(200, "text/plain; charset=utf-8", network_.apAlwaysOn()
        ? "Настройки Wi-Fi сохранены. После перезагрузки снова подключитесь к G5500-Rotator или откройте адрес контроллера в домашней сети."
        : "Настройки Wi-Fi сохранены. После перезагрузки откройте адрес контроллера в домашней сети. Если подключение не удалось, появится G5500-Rotator.");
    restart_.request(millis());

    return;
  }
  if(path=="/api/wifi" && method=="POST") {
    const String ssid = web_.arg("ssid"), password = web_.arg("pass");
    const auto result = storage_.saveWifi(ssid, password);
    if (result != WifiSaveResult::Ok) {
      diagnostics_.dbgPrintln(String("[WIFI NVS] save failed, stage=") +
                             (result == WifiSaveResult::InvalidSsid ? "invalid_ssid" :
                              result == WifiSaveResult::InvalidPassword ? "invalid_password" :
                              result == WifiSaveResult::OpenFailed ? "open" :
                              result == WifiSaveResult::WriteFailed ? "write" : "verify"));
      const bool invalid = result == WifiSaveResult::InvalidSsid || result == WifiSaveResult::InvalidPassword;
      const char *message = result == WifiSaveResult::InvalidSsid ? "SSID должен содержать от 1 до 32 байт." :
                            result == WifiSaveResult::InvalidPassword ? "Пароль должен содержать 8–63 символа, 64 шестнадцатеричные цифры или быть пустым для открытой сети." :
                            "Не удалось сохранить настройки Wi-Fi. Перезагрузка отменена.";
      web_.send(invalid ? 400 : 500, "text/plain; charset=utf-8", message);
      return;
    }
    diagnostics_.dbgPrintln("[WIFI NVS] credentials saved and verified; restarting");
    web_.send(200, "text/plain; charset=utf-8", network_.apAlwaysOn()
        ? "Настройки Wi-Fi сохранены. После перезагрузки снова подключитесь к G5500-Rotator или откройте адрес контроллера в домашней сети."
        : "Настройки Wi-Fi сохранены. После перезагрузки откройте адрес контроллера в домашней сети. Если подключение не удалось, появится G5500-Rotator.");
    restart_.request(millis());

    return;
  }
  if(method=="POST" && (path=="/api/mode" || path=="/api/execute" || path=="/api/preset" || path=="/api/preset/run" || path=="/api/settings" || path=="/api/motion/reset" || path=="/api/motion/autotune")) {
      auto oldModel = runtime_.controller.model;
      auto oldSettings = runtime_.loggerSettings;
      actions_.updatePosition();
      auto response = callApi(path,arguments);
      if (response.status == 200) {
        if (oldModel != runtime_.controller.model) {
          // Old calibration was already saved on each calibration edit.
          actions_.loadPrefs();
          actions_.loadMotionSettings();
          runtime_.controller.setRange(calibration_.activeAzRange(), calibration_.state().gsAzMode, millis());
          actions_.loadProfilePresets();
          actions_.invalidateDisplay();
          runtime_.targetValid = false;
          // Reapply the explicit simple/settings fields to the newly loaded
          // profile. Combined model + PID changes are rejected by the API.
          if(path == "/api/settings") response = callApi(path,arguments);
        }
        if (path == "/api/mode" || path == "/api/settings" || (path=="/api/motion/autotune" && web_.arg("action")=="apply")) {
          actions_.saveForkSettings();
          if(runtime_.controller.fault=="motion_storage_failed") {
            if(path=="/api/motion/autotune") {runtime_.controller.autoTune.phase=AutoTunePhase::Failed;runtime_.controller.autoTune.reason="motion_storage_failed";}
            response={503,"{\"ok\":false,\"result\":\"motion_storage_failed\"}"};
          }
        }
        if (path == "/api/preset") {
          actions_.saveProfilePresets();
        }
        if (oldSettings.n1mmPort != runtime_.loggerSettings.n1mmPort ||
            oldSettings.pstPort != runtime_.loggerSettings.pstPort || oldSettings.commandPort != runtime_.loggerSettings.commandPort) {
          network_.setupLoggerUdp(); network_.setupCommandUdp();
        }
        if (path == "/api/execute" || path == "/api/preset/run") {
          runtime_.targetValid = runtime_.controller.targetKnown;
        }
      }
      runtime_.controller.tick(millis());
      hardware_.applyOutputs();
      web_.send(response.status, "application/json", response.body.c_str());

    return;
  }
  reply(404,"not_found");
}
}
