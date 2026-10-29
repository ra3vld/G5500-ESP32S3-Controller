#include "wifi_access.hpp"
// Device configuration and control shared by HTTP, UART/USB, TCP and UDP. GPL-3.0.
#include "device_api.hpp"
#include "feature_support.hpp"
#include "command_api.hpp"
#include "hardware_form.hpp"
#include "pin_json.hpp"
#include "motion_json.hpp"
#include "runtime_metrics.hpp"
#include "panel_metrics.hpp"
#include "tcp_memory.hpp"
#include "network_timing.hpp"
#include <esp_timer.h>
namespace forkrot {
using namespace board;
namespace {
void writeHardwareTrial(JsonWriter &j, const Runtime &runtime) {
  j << ",\"hardwareTrial\":";
  if(!runtime.hardwareTrial) { j << "null"; return; }
  const auto elapsed=uint32_t(millis()-runtime.memoryGuard.started);
  const bool ready=runtime.memoryGuard.ready(millis()) && heapFreeBytes()>=MemoryGuard::HealthyFree &&
    heapMaxBlockBytes()>=MemoryGuard::HealthyBlock && runtimeMetrics.allocationFailures.load()==runtime.memoryGuard.failures;
  j << "{\"remainingMs\":" << (elapsed<MemoryGuard::TrialMs?MemoryGuard::TrialMs-elapsed:0u)
    << ",\"ready\":" << (ready?"true":"false") << '}';
}
void writePreviousAllocation(JsonWriter &j,unsigned index) {
  const auto &r=runtimeMetrics.previousFailures[index];
  j << "{\"ready\":" << (allocationRecordValid(r)?"true":"false");
  if(allocationRecordValid(r)) {
    char elf[65];for(unsigned i=0;i<32;++i)std::snprintf(elf+i*2,sizeof elf-i*2,"%02x",unsigned(r.elf[i]));
    j << ",\"size\":" << r.size << ",\"caps\":" << r.caps << ",\"free\":" << r.freeBytes
      << ",\"largestBlock\":" << r.largestBlock << ",\"task\":" << jsonQuoted(r.task)
      << ",\"function\":" << jsonQuoted(r.function) << ",\"elf\":" << jsonQuoted(elf) << ",\"pc\":[";
    for(unsigned n=0;n<8;++n){if(n)j << ',';j << r.pc[n];}j << ']';
  }
  j << '}';
}
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
      << ",\"pwmInverted\":" << unsigned(c.pwmInverted) << ",\"pins\":";
    writePinMap(j,c.pins);
    for(unsigned i=0;i<11;++i) j << ',' << jsonQuoted(localSettingNames[i]) << ':' << c.local[i];
    for(unsigned i=0;i<SensorSettings{}.size();++i) j << ',' << jsonQuoted(sensorSettingNames[i]) << ':' << c.sensors[i];
    j << '}';
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
  j << "{\"features\":";writeBuildFeatures(j);
  j << ",\"role\":" << jsonQuoted(runtime_.configuredAsHud()?"hud":"controller") << ",\"token\":" << runtime_.hardwareToken << ",\"generation\":" << r.generation
    << ",\"hasConfirmed\":" << (r.hasConfirmed ? "true" : "false")
    << ",\"trial\":" << (runtime_.hardwareTrial ? "true" : "false")
    << ",\"recovery\":" << (runtime_.recovery ? "true" : "false")
    << ",\"reason\":" << jsonQuoted(recoveryReasonName(runtime_.recoveryReason))
    << ",\"failureCount\":" << unsigned(runtime_.recoveryHistory.failures)
    << ",\"failureLimit\":" << unsigned(RecoveryHistory::Limit)
    << ",\"failureResetMs\":" << RecoveryHistory::StableMs
    << ",\"lastFailure\":" << jsonQuoted(recoveryReasonName(runtime_.recoveryHistory.last))
    << ",\"ready\":" << (ready ? "true" : "false")
    << ",\"remainingMs\":" << (runtime_.hardwareTrial && elapsed < MemoryGuard::TrialMs ? MemoryGuard::TrialMs-elapsed : 0u)
    << ",\"heapFree\":" << heapFreeBytes() << ",\"heapMaxBlock\":" << heapMaxBlockBytes()
    << ",\"backend\":" << jsonQuoted(runtime_.controller.hardware.backendName())
    << ",\"sppSupported\":" << (FORK_CLASSIC_ESP32 && BLUETOOTH_SPP_SUPPORT ? "true" : "false")
    << ",\"classicEsp32\":" << (FORK_CLASSIC_ESP32 ? "true" : "false")
    << ",\"crowpanel\":" << (SELECTED_BOARD == BOARD_CROWPANEL_21 ? "true" : "false")
    << ",\"crowpanelLocalDrive\":" << (CROWPANEL_LOCAL_DRIVE ? "true" : "false")
    << ",\"separateDrive\":" << (CROWPANEL_LOCAL_DRIVE?"false":"true")
    << ",\"btsDrive\":true,\"independentDrive\":true"
    << ",\"setupRequired\":" << (runtime_.setup.required?"true":"false")
    << ",\"setupGeneration\":" << runtime_.setup.generation
    << ",\"setupMotionAllowed\":" << (setupMotionAllowed()?"true":"false")
    << ",\"configured\":"; config(configured);
  if(view!="form") {
    j << ",\"effective\":"; config(runtime_.controller.hardware);
    j << ",\"confirmed\":"; config(r.good);
  }
  j << '}';

}
Response DeviceApi::callApi(std::string_view path, FormView form) {
  if(path=="/api/settings") {
    unsigned n=runtime_.loggerSettings.n1mmPort, p=runtime_.loggerSettings.pstPort,
      r=runtime_.loggerSettings.reportPort, command=runtime_.loggerSettings.commandPort;
    for(auto e:form) {
      if(!featureSettingEnabled(e.first))return {501,"{\"ok\":false,\"result\":\"feature_disabled\"}"};
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
  j << "{\"model\":" << jsonQuoted(modelName(c.model))
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
    << ",\"azSensor\":" << jsonQuoted(sensorName(c.hardware.sensorType(0)))
    << ",\"elSensor\":" << jsonQuoted(sensorName(c.hardware.sensorType(1)))
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
    << ",\"bluetoothSupported\":" << (BLUETOOTH_SUPPORT?"true":"false") << ",\"features\":"; writeBuildFeatures(j); j << "}}";
}
bool DeviceApi::setupReady(SetupStep step) const {
  const auto &c=runtime_.controller;
  if(runtime_.recovery || runtime_.hardwareTrial)return false;
  switch(step) {
    case SetupStep::Network: {
      if(!WIFI_SUPPORT && !ETHERNET_SUPPORT)return COMMAND_API_SUPPORT;
      switch(network_.credentials().network.choice) {
        case NetworkChoice::Ap: case NetworkChoice::SharedHost:return (radioWifi.getMode()&WIFI_AP) && uint32_t(radioWifi.softAPIP());
        case NetworkChoice::Ethernet: {
          if(!ethernetConnected())return false;
          const auto &ip=network_.credentials().network.ethernet;char expected[16];formatIpv4(ip.address,expected);
          return ip.automatic || ethernetIP().toString()==expected;
        }
        default: {
          if(radioWifi.status()!=WL_CONNECTED || !uint32_t(radioWifi.localIP()))return false;
          const auto &ip=network_.credentials().network.wifi;char expected[16];formatIpv4(ip.address,expected);
          return ip.automatic || radioWifi.localIP().toString()==expected;
        }
      }
    }
    case SetupStep::Equipment:return runtime_.hardwareRecord.hasConfirmed && !c.configurationLocked &&
      (runtime_.hud.isHud() || runtime_.setup.drive<0 || (c.hardware.drive==runtime_.setup.drive && c.hardware.simulation==runtime_.setup.simulation));
    case SetupStep::Calibration:return c.hardware.simulation || c.hardware.manualWithoutFeedback() || (c.axes[0].calibrated && (c.model!=Model::G5500 || c.axes[1].calibrated));
    case SetupStep::Tuning:
      if(c.autoTune.active() || c.coast.active() || c.hasDriveError())return false;
      if(c.hardware.manualWithoutFeedback())return true;
      for(unsigned i=0;i<(c.model==Model::G5500?2u:1u);++i)
        if(c.variableSpeed(i) ? (!c.motionSettings[i].ready || !c.motionSettings[i].valid()) :
            (runtime_.setup.fresh && !c.hardware.simulation && !(runtime_.setup.checked&(1u<<(12+i)))))return false;
      return true;
    case SetupStep::Peers:return runtime_.hud.isHud() && runtime_.hud.active() && runtime_.hud.active()->saved && c.remote && c.available() && c.axes[0].calibrated &&
      (c.model!=Model::G5500 || c.axes[1].calibrated) && !c.hasDriveError();
    case SetupStep::Review:
      return setupReady(SetupStep::Network) && setupReady(SetupStep::Equipment) &&
        (runtime_.hud.isHud()?setupReady(SetupStep::Peers):c.hardware.simulation || (setupReady(SetupStep::Calibration)&&setupReady(SetupStep::Tuning))) && !c.hasDriveError();
    default:return true;
  }
}
bool DeviceApi::setupMotionAllowed() const {
  return !runtime_.recovery && !runtime_.controller.movementBlocked();
}
void DeviceApi::writeSetup(JsonWriter &j) const {
  const auto &s=runtime_.setup;
  j << "{\"version\":1,\"required\":" << (s.required?"true":"false") << ",\"paused\":" << (s.paused?"true":"false")
    << ",\"step\":" << unsigned(s.step) << ",\"checked\":" << s.checked << ",\"generation\":" << s.generation
    << ",\"equipment\":" << (setupReady(SetupStep::Equipment)?"true":"false") << ",\"ready\":" << (setupReady(s.step)?"true":"false")
    << ",\"motionAllowed\":" << (setupMotionAllowed()?"true":"false")
    << ",\"role\":" << jsonQuoted(runtime_.hud.isHud()?"hud":"controller")
    << ",\"driveInterface\":" << unsigned(s.drive<0?runtime_.controller.hardware.drive:s.drive)
    << ",\"separateDrive\":" << (CROWPANEL_LOCAL_DRIVE?"false":"true")
    << ",\"simulation\":" << ((s.drive<0?runtime_.controller.hardware.simulation:s.simulation)?"true":"false")
    << ",\"manualOnly\":" << (runtime_.controller.hardware.manualWithoutFeedback()?"true":"false")
    << ",\"testToken\":" << runtime_.setupTestToken << ",\"token\":" << runtime_.setupLease << ",\"storageOk\":" << (runtime_.setupStorageOk?"true":"false")
    << ",\"restartRequired\":" << (runtime_.setupRestartRequired?"true":"false") << ",\"pendingNext\":" << (s.pendingNext?"true":"false") << '}';
}
void DeviceApi::configurationRestart(bool setupBatch) {
  if(setupBatch && runtime_.setupStorageOk)runtime_.setupRestartRequired=true;
  else restart_.request(millis());
}
void DeviceApi::serviceSetup(uint32_t now) {
  if(runtime_.setup.required && runtime_.setup.pendingNext && !runtime_.setup.paused &&
      !runtime_.setupSaving && !runtime_.setupRestartRequired && !restart_.pending() && setupReady(runtime_.setup.step)) {
    auto next=runtime_.setup;next.checked|=uint16_t(1u<<unsigned(next.step));
    next.step=next.next(runtime_.hud.isHud(),runtime_.controller.hardware.simulation || runtime_.controller.hardware.manualWithoutFeedback());next.pendingNext=false;++next.generation;
    if(storage_.saveSetup(next))runtime_.setup=next;
    else {runtime_.setupStorageOk=false;runtime_.setup.pendingNext=false;}
  }
  if(!runtime_.setup.required || runtime_.setup.paused){runtime_.setupTestToken=0;return;}
  if(!runtime_.setupTestToken || uint32_t(now-runtime_.setupRenewed)<=Runtime::SetupTestTimeoutMs)return;
  runtime_.setupTestToken=0;
  auto &c=runtime_.controller;c.cancelCoast(now);c.cancelAutoTune(now);c.stopNow(-1,now);hardware_.applyOutputs();
}
void DeviceApi::invalidateSetup(SetupStep step) {
  if(!runtime_.setup.required || runtime_.setup.paused)return;
  auto next=runtime_.setup;
  next.pendingNext=false;
  if(step==SetupStep::Role || step==SetupStep::Equipment)next.equipment=false;
  if(step==SetupStep::Role || step==SetupStep::Model || step==SetupStep::Equipment || step==SetupStep::Calibration){
    next.motionConfirmed=false;
  }
  if(step==SetupStep::Equipment && !runtime_.setupSaving){next.drive=-1;next.simulation=false;}
  if(unsigned(next.step)>unsigned(step))next.step=step;
  next.checked&=uint16_t((1u<<unsigned(step))-1);++next.generation;
  if(!storage_.saveSetup(next))runtime_.setupStorageOk=false;
  else runtime_.setup=next;
}

void DeviceApi::execute(std::string_view method, std::string_view path, FormView arguments,
                        JsonWriter &output, int &status, const char *&type, bool command) {
  ApiContext web_(arguments,output,status,type);
  auto reply=[&](int code,const char *reason) {
    status=code; type="application/json";
    output << "{\"ok\":" << (code==200?"true":"false") << ",\"error\":" << jsonQuoted(reason) << '}';
  };
  if(!featurePathEnabled(path)) { reply(501,"feature_disabled"); return; }
  const bool hardwarePath=path=="/api/hardware" || path.substr(0,14)=="/api/hardware/";
  const bool stop=path=="/api/move" && (web_.value("d")=="S" || web_.value("d")=="A" || web_.value("d")=="E");
  if(ota_.locked() && !stop && path!="/api/status" && path!="/api/diaglog" && path!="/api/diagnostics" && path.substr(0,8)!="/api/ota") { reply(503,"update_locked"); return; }
  if(runtime_.recovery && !stop && !hardwarePath && path!="/api/status" && path!="/api/network" && path!="/api/diagnostics" && path!="/api/diaglog" && path!="/api/resetreason" && path!="/api/resethistory" && path!="/api/wifi" && path!="/api/ap") { reply(409,"recovery_only"); return; }
  serviceSetup(millis());
  uint32_t lease=0,testToken=0;
  const bool setupSession=!command && hardwareUnsigned(web_.value("setupToken"),lease) && lease && lease==runtime_.setupLease &&
      runtime_.setup.required && !runtime_.setup.paused;
  const bool testFresh=hardwareUnsigned(web_.value("setupTestToken"),testToken) && testToken && testToken==runtime_.setupTestToken &&
      uint32_t(millis()-runtime_.setupRenewed)<=Runtime::SetupTestTimeoutMs;
  if(!stop && path!="/api/setup" && web_.hasArg("setupTestToken") && (!setupSession || !testFresh)){reply(503,"setup_test_expired");return;}
  // Keep the general API's temporary buffers off recovery and hardware paths.
  const bool setupBatch=setupSession && runtime_.setupSaving;
  if(hardwarePath)executeHardware(method,path,arguments,output,status,type,command,setupBatch);
  else executeGeneral(method,path,arguments,output,status,type,command,lease,setupBatch);
}

void DeviceApi::executeHardware(std::string_view method, std::string_view path, FormView arguments,
                                JsonWriter &output, int &status, const char *&type, bool command, bool setupBatch) {
  ApiContext web_(arguments,output,status,type);
  auto reply=[&](int code,const char *reason) {
    status=code; type="application/json";
    output << "{\"ok\":" << (code==200?"true":"false") << ",\"error\":" << jsonQuoted(reason) << '}';
  };
  if(path=="/api/hardware/display") {
    const auto &current=runtime_.displaySettings;
    if(method=="GET") {
      constexpr unsigned w=DISPLAY_KIND==6?360:DISPLAY_KIND==5?480:DISPLAY_KIND==3?480:DISPLAY_KIND==4?240:320;
      constexpr unsigned h=DISPLAY_KIND==6?360:DISPLAY_KIND==5?320:DISPLAY_KIND==3?480:240;
      output << "{\"supported\":" << (GRAPHICAL_DISPLAY?"true":"false") << ",\"displayKind\":" << DISPLAY_KIND
        << ",\"token\":" << runtime_.hardwareToken << ",\"revision\":" << current.revision
        << ",\"storageOk\":" << (runtime_.displayStorageOk?"true":"false")
        << ",\"restartRequired\":" << (runtime_.displayRestartPending?"true":"false")
        << ",\"width\":" << (GRAPHICAL_DISPLAY?current.width(w,h):0) << ",\"height\":" << (GRAPHICAL_DISPLAY?current.height(w,h):0);
      for(unsigned i=0;i<2;++i){const unsigned pose=i?current.touch:current.screen;const char *p=i?"touch":"screen";
        output << ",\"" << p << "Rotation\":" << (pose&3)*90 << ",\"" << p << "MirrorX\":" << unsigned(bool(pose&4))
          << ",\"" << p << "MirrorY\":" << unsigned(bool(pose&8));}
      for(unsigned i=0;i<6;++i)output << ",\"" << DisplayFieldNames[i] << "\":" << unsigned(bool(current.visible&(1u<<i)));
      output << ",\"defaultFields\":" << unsigned(defaultDisplayFields(DISPLAY_KIND));
      output << '}';return;
    }
    if(method!="POST"){reply(405,"method_not_allowed");return;}
    if(!GRAPHICAL_DISPLAY){reply(409,"display_unavailable");return;}
    if(runtime_.displayRestartPending){reply(409,"restarting");return;}
    DisplaySettings next=current;uint32_t token=0,revision=0;unsigned seen=0;
    constexpr const char *fields[]={"token","revision","screenRotation","screenMirrorX","screenMirrorY","touchRotation","touchMirrorX","touchMirrorY",
      "showName","showControllerAddress","showLocalAddress","showApAddress","showHeap","showAxis"};
    for(auto e:arguments) {
      unsigned i=0;while(i<14&&e.first!=fields[i])++i;uint32_t value=0;
      if(i==14 || (seen&(1u<<i)) || !hardwareUnsigned(e.second,value)){reply(400,"invalid_form");return;}
      seen|=1u<<i;
      if(i==0)token=value;
      else if(i==1)revision=value;
      else if(i>=8) {
        if(value>1){reply(400,"invalid_visibility");return;}
        const unsigned bit=1u<<(i-8);next.visible=uint8_t((next.visible&~bit)|(value?bit:0));
      } else {
        auto &pose=i>=5?next.touch:next.screen;
        const unsigned part=(i-2)%3;
        if(!part){if(value>270||value%90){reply(400,"invalid_rotation");return;}pose=uint8_t((pose&12)|value/90);}
        else {if(value>1){reply(400,"invalid_mirror");return;}const unsigned bit=part==1?4:8;pose=uint8_t((pose&~bit)|(value?bit:0));}
      }
    }
    if((seen&3)!=3 || !(seen&0x3ffc)){reply(400,"invalid_form");return;}
    if(token!=runtime_.hardwareToken || revision!=current.revision){reply(409,"stale_configuration");return;}
    if(runtime_.displayStorageOk && next.sameSettings(current)){reply(200,"");return;}
    hardware_.cancelJoystickCalibration();runtime_.controller.stopNow(-1,millis());hardware_.applyOutputs();
    ++next.revision;
    if(!storage_.saveDisplay(next)){reply(503,"storage_failed");return;}
    runtime_.displayRestartPending=true;runtime_.controller.configurationLocked=true;
    reply(200,"");restart_.request(millis());return;
  }
  if(path=="/api/hardware/modbus") {
    const auto &c=runtime_.modbus;
    if(method=="GET") {
      const auto &s=runtime_.modbusStatus;
      output << "{\"pc\":" << unsigned(c.pc) << ",\"bus\":" << unsigned(c.bus)
        << ",\"address\":" << unsigned(c.address) << ",\"node\":" << unsigned(c.node)
        << ",\"first\":" << unsigned(c.first) << ",\"count\":" << unsigned(c.count)
        << ",\"uart0Baud\":" << c.uart0.baud << ",\"uart0Parity\":" << unsigned(c.uart0.parity) << ",\"uart0StopBits\":" << unsigned(c.uart0.stopBits)
        << ",\"uart1Baud\":" << c.uart1.baud << ",\"uart1Parity\":" << unsigned(c.uart1.parity) << ",\"uart1StopBits\":" << unsigned(c.uart1.stopBits)
        << ",\"enabled\":" << (!runtime_.recovery&&(c.pc||c.bus)?"true":"false") << ",\"owner\":" << unsigned(s.owner)
        << ",\"received\":" << s.received << ",\"rejected\":" << s.rejected << ",\"timeouts\":" << s.timeouts
        << ",\"passes\":" << s.passes << ",\"recoveries\":" << s.recoveries << ",\"fault\":" << (s.fault?"true":"false") << '}';return;
    }
    if(method!="POST"){reply(405,"method_not_allowed");return;}
    ModbusConfig next=c;uint16_t seen=0;uint32_t token=0,generation=0;
    for(auto e:arguments){
      unsigned i=0;
      if(e.first=="token")i=0;
      else if(e.first=="generation")i=1;
      else {i=2;while(i<14&&e.first!=SerialFieldNames[i-2])++i;}
      uint32_t v=0;
      if(i==14 || (seen&(1u<<i)) || !hardwareUnsigned(e.second,v) || (i>=2&&!setSerialField(next,i-2,v))) {reply(400,"invalid_form");return;}
      seen|=1u<<i;
      if(i==0)token=v;else if(i==1)generation=v;
    }
    if((seen&3)!=3 || token!=runtime_.hardwareToken || generation!=runtime_.hardwareRecord.generation){reply(409,"stale_configuration");return;}
    const auto &record=runtime_.hardwareRecord;
    const auto &hardware=runtime_.recovery?record.candidate:runtime_.controller.hardware;
    if(const auto error=next.wiring(hardware,runtime_.hardwareHudUart)){reply(400,error);return;}
    if(!storage_.saveModbus(next)){reply(503,"storage_failed");return;}
    hardware_.cancelJoystickCalibration();runtime_.controller.stopNow(-1,millis());hardware_.applyOutputs();
    runtime_.controller.configurationLocked=true;reply(200,"");configurationRestart(setupBatch);return;
  }
  if(method=="GET" && path=="/api/hardware/pins") { writePinCapabilities(output,runtime_.hardwareHudUart); return; }
  if(method=="GET" && path=="/api/hardware") { writeHardwareStatus(output,command,web_.value("view")); return; }
  if(method=="GET" && path=="/api/hardware/reboots") {
    output << "{\"current\":{\"reason\":" << jsonQuoted(diagnostics_.bootReasonText().c_str())
      << ",\"code\":" << diagnostics_.bootReasonCode() << "},\"recoveryReason\":"
      << jsonQuoted(recoveryReasonName(runtime_.recoveryReason)) << ",\"history\":";
    storage_.writeResetHistoryJson(output);output << ",\"previousAllocations\":[";
    writePreviousAllocation(output,0);output << ',';writePreviousAllocation(output,1);output << "]}";return;
  }
  if(method!="POST") { reply(405,"method_not_allowed"); return; }
  HardwareForm form;
  auto modbus=runtime_.modbus;
  const auto &r=runtime_.hardwareRecord;
  const auto &base=r.phase==ConfigPhase::Testing || r.phase==ConfigPhase::Pending || runtime_.recovery?r.candidate:r.good;
  if(!parseHardwareArguments(arguments,form,path=="/api/hardware",base,path=="/api/hardware"?&modbus:nullptr)) { reply(400,"invalid_form"); return; }
  if(path=="/api/hardware" && (
      (!LOCAL_INPUT_SUPPORT && form.config.local!=base.local) ||
      (!LOCAL_INPUT_SUPPORT && form.config.buttonGpio!=base.buttonGpio) ||
      (!BLUETOOTH_SUPPORT && form.config.bluetooth!=base.bluetooth) ||
      (!BLUETOOTH_SPP_SUPPORT && form.config.bluetoothMode==BT_ESP32_SPP && base.bluetoothMode!=BT_ESP32_SPP) ||
      (!WIFI_SUPPORT && form.config.forceAp!=base.forceAp) ||
      (!MODBUS_RTU_SUPPORT && !(modbus==runtime_.modbus)) ||
      (!RS485_SUPPORT && (form.config.pins.uartDe!=base.pins.uartDe || form.config.pins.hudDe!=base.pins.hudDe)))) {reply(501,"feature_disabled");return;}
  if(form.token!=runtime_.hardwareToken || form.generation!=runtime_.hardwareRecord.generation) { reply(409,"stale_configuration"); return; }
  if(path=="/api/hardware/joystick/start" || path=="/api/hardware/joystick/poll" || path=="/api/hardware/joystick/cancel") {
    if(path=="/api/hardware/joystick/cancel") { hardware_.cancelJoystickCalibration(); reply(200,""); return; }
    const bool ok=path=="/api/hardware/joystick/start"?hardware_.startJoystickCalibration():hardware_.pollJoystickCalibration();
    if(!ok) { reply(409,"joystick_calibration_inactive"); return; }
    output << "{\"ok\":true,\"raw\":[" << hardware_.joystickRaw(0) << ',' << hardware_.joystickRaw(1) << "]}"; return;
  }
  bool saved=false;
  if(path=="/api/hardware/probe") {
    const bool ok=!runtime_.recovery && runtime_.hardwareTrial && runtime_.memoryGuard.ready(millis()) && storage_.probeHardwareStorage(runtime_);
    runtime_.hardwareProbe=ok; reply(ok?200:409,ok?"":"not_ready"); return;
  }
  auto stopForConfiguration=[&] {
    hardware_.cancelJoystickCalibration();
    runtime_.controller.stopNow(-1,millis()); hardware_.applyOutputs(); runtime_.controller.configurationLocked=true;
  };
  if(path=="/api/hardware") {
    if(!runtime_.configuredAsHud() && setupBatch && runtime_.setup.drive>=0 &&
        (form.config.drive!=runtime_.setup.drive || form.config.simulation!=runtime_.setup.simulation)) {reply(409,"setup_drive_mismatch");return;}
    if(const auto error=board::validateHardwareDetailed(form.config,runtime_.hardwareHudUart,runtime_.configuredAsHud())) {
      status=400;
      output << "{\"ok\":false,\"error\":" << jsonQuoted(error.error) << ",\"field\":" << jsonQuoted(error.field)
        << ",\"gpio\":" << error.pin << ",\"owner\":" << jsonQuoted(error.owner) << '}'; return;
    }
    if(const auto error=modbus.wiring(form.config,runtime_.hardwareHudUart)){reply(400,error);return;}
    const bool hardwareChanged=!setupBatch || !(form.config==base);
    const bool modbusChanged=!(modbus==runtime_.modbus);
    if(!hardwareChanged && !modbusChanged){reply(200,"");return;}
    if(modbusChanged && !storage_.saveModbus(modbus)){reply(503,"storage_failed");return;}
    if(hardwareChanged)invalidateSetup(SetupStep::Equipment);
    stopForConfiguration();
    saved=!hardwareChanged || storage_.stageHardware(runtime_,form.config);
    if(!saved && modbusChanged)storage_.saveModbus(runtime_.modbus);
  } else if(path=="/api/hardware/confirm") {
    if(runtime_.recovery || !runtime_.hardwareTrial || !runtime_.hardwareProbe || !runtime_.memoryGuard.ready(millis()) || heapFreeBytes()<MemoryGuard::HealthyFree || heapMaxBlockBytes()<MemoryGuard::HealthyBlock || runtimeMetrics.allocationFailures.load()!=runtime_.memoryGuard.failures) { reply(409,"not_ready"); return; }
    saved=storage_.confirmHardware(runtime_);
    if(saved && setupBatch) {auto next=runtime_.setup;next.equipment=true;next.step=SetupStep::Equipment;next.pendingNext=false;++next.generation;
      runtime_.setupStorageOk=storage_.saveSetup(next);if(runtime_.setupStorageOk)runtime_.setup=next;}
  } else if(path=="/api/hardware/rollback") {
    if(!runtime_.hardwareRecord.hasConfirmed) { reply(409,"no_confirmed_configuration"); return; }
    auto good=runtime_.hardwareRecord.good;
    if(!BLUETOOTH_SUPPORT)good.bluetooth=false;
    if(const auto error=board::validateHardwareDetailed(good,runtime_.hardwareHudUart,runtime_.configuredAsHud())) {
      status=400;
      output << "{\"ok\":false,\"error\":" << jsonQuoted(error.error) << ",\"field\":" << jsonQuoted(error.field)
        << ",\"gpio\":" << error.pin << ",\"owner\":" << jsonQuoted(error.owner) << '}';return;
    }
    stopForConfiguration(); saved=storage_.stageHardware(runtime_,good);
  } else if(path=="/api/hardware/recovery") {
    stopForConfiguration(); saved=storage_.enterRecovery(runtime_,RecoveryReason::Requested);
  }
  else { reply(404,"not_found"); return; }
  if(saved && (path=="/api/hardware" || path=="/api/hardware/rollback") &&
      runtime_.hardwareRecord.phase==ConfigPhase::Confirmed && setupBatch && !runtime_.setup.equipment) {
    auto next=runtime_.setup;next.equipment=true;++next.generation;
    runtime_.setupStorageOk=storage_.saveSetup(next);if(runtime_.setupStorageOk)runtime_.setup=next;
  }
  if(!saved) { reply(503,"storage_failed"); return; }
  output << "{\"ok\":true,\"error\":\"\",\"confirmationRequired\":"
    << (runtime_.hardwareRecord.phase==ConfigPhase::Pending?"true":"false") << '}';
  if(path!="/api/hardware/confirm")configurationRestart(setupBatch);
  return;
}

void DeviceApi::executeGeneral(std::string_view method, std::string_view path, FormView arguments,
                               JsonWriter &output, int &status, const char *&type, bool command, uint32_t lease, bool setupBatch) {
  ApiContext web_(arguments,output,status,type);
  auto reply=[&](int code,const char *reason) {
    status=code; type="application/json";
    output << "{\"ok\":" << (code==200?"true":"false") << ",\"error\":" << jsonQuoted(reason) << '}';
  };
  if(path=="/api/setup") {
    if(method=="GET"){writeSetup(output);return;}
    if(method!="POST" || command){reply(405,"method_not_allowed");return;}
    const auto action=web_.value("action");
    if(action=="arm" || action=="renew") {
      if(!lease || lease!=runtime_.setupLease || !runtime_.setup.required || runtime_.setup.paused){reply(409,"setup_lease_expired");return;}
      if(runtime_.setup.step!=SetupStep::Calibration && runtime_.setup.step!=SetupStep::Tuning && runtime_.setup.step!=SetupStep::Review){reply(409,"setup_step_not_ready");return;}
      uint32_t testToken=0;
      if(action=="renew" && (!hardwareUnsigned(web_.value("setupTestToken"),testToken) || !testToken || testToken!=runtime_.setupTestToken)) {
        reply(409,"setup_test_expired");return;
      }
      if(!runtime_.setupTestToken) {
        if(!++runtime_.setupTestEpoch)++runtime_.setupTestEpoch;
        runtime_.setupTestToken=runtime_.setupTestEpoch;
      }
      runtime_.setupRenewed=millis();
      if(action=="arm")writeSetup(output);else reply(200,"");
      return;
    }
    if(action=="begin_save" || action=="end_save") {
      if(!lease || lease!=runtime_.setupLease || runtime_.setup.paused){reply(409,"setup_lease_expired");return;}
      runtime_.setupSaving=action=="begin_save";
      if(runtime_.setupSaving)runtime_.setupTestToken=0;
      writeSetup(output);return;
    }
    auto next=runtime_.setup;
    if(action=="drive") {
      uint32_t generation=0,drive=0,simulation=0;
      if(!lease || lease!=runtime_.setupLease || !next.required || next.paused || next.step!=SetupStep::Model){reply(409,"setup_step_not_ready");return;}
      if(!hardwareUnsigned(web_.value("generation"),generation)||generation!=next.generation){reply(409,"stale_setup");return;}
      if(!hardwareUnsigned(web_.value("driveInterface"),drive)||drive>DRIVE_DIR_PWM ||
          !hardwareUnsigned(web_.value("simulation"),simulation)||simulation>1 || (CROWPANEL_LOCAL_DRIVE && (drive==DRIVE_SEPARATE_PWM || drive==DRIVE_DIR_PWM))) {reply(400,"invalid_hardware");return;}
      next.drive=int8_t(drive);next.simulation=simulation;
      if(drive!=runtime_.controller.hardware.drive || bool(simulation)!=runtime_.controller.hardware.simulation){next.equipment=false;next.motionConfirmed=false;}
    } else if(action=="start" || action=="resume") {
      if(!next.required) {next.required=true;next.motionConfirmed=false;next.equipment=runtime_.hardwareRecord.hasConfirmed;next.checked=0;next.step=SetupStep::Network;}
      next.paused=false;
    } else if(action=="pause") {
      next.paused=true;next.pendingNext=false;
    } else if(action=="back") {
      if(!next.required){reply(409,"setup_inactive");return;}
      next.pendingNext=false;
      SetupState cursor;cursor.step=SetupStep::Network;
      while(cursor.next(runtime_.hud.isHud(),runtime_.controller.hardware.simulation || runtime_.controller.hardware.manualWithoutFeedback())!=next.step && cursor.step!=SetupStep::Review && cursor.step!=SetupStep::Done)
        cursor.step=cursor.next(runtime_.hud.isHud(),runtime_.controller.hardware.simulation || runtime_.controller.hardware.manualWithoutFeedback());
      next.step=next.step==SetupStep::Network?SetupStep::Network:cursor.step;
    } else if(action=="apply") {
      if(!lease || lease!=runtime_.setupLease || next.paused || !runtime_.setupRestartRequired){reply(409,"setup_step_not_ready");return;}
      next.pendingNext=true;
    } else if(action=="advance" || action=="complete") {
      uint32_t generation=0;
      if(!hardwareUnsigned(web_.value("generation"),generation)||generation!=next.generation){reply(409,"stale_setup");return;}
      if(!next.required || next.paused || (runtime_.setupRestartRequired && next.step!=SetupStep::Review) || !setupReady(next.step)){reply(409,"setup_step_not_ready");return;}
      if(action=="complete" && next.step!=SetupStep::Review){reply(409,"setup_step_not_ready");return;}
      next.checked|=uint16_t(1u<<unsigned(next.step));
      const bool finished=next.step==SetupStep::Review;
      next.step=next.next(runtime_.hud.isHud(),runtime_.controller.hardware.simulation || runtime_.controller.hardware.manualWithoutFeedback());
      next.pendingNext=false;
      if(finished){next.required=false;next.paused=false;next.fresh=false;next.drive=-1;next.simulation=false;}
    } else {reply(400,"invalid_setup_action");return;}
    ++next.generation;
    if(!storage_.saveSetup(next)){
      runtime_.setupStorageOk=false;
      if(runtime_.setupRestartRequired && (action=="apply" || action=="pause" || action=="complete")) {
        runtime_.setupSaving=false;restart_.request(millis());
      }
      reply(503,"setup_storage_failed");return;
    }
    runtime_.setupStorageOk=true;runtime_.setup=next;runtime_.controller.setupRequired=next.required;
    runtime_.setupTestToken=0;
    if(next.paused || !next.required)runtime_.setupLease=0;
    if(!next.required && runtime_.setupRestartRequired)restart_.request(millis());
    if(action=="start"||action=="resume") {
      runtime_.setupLease=(runtime_.hardwareToken^(next.generation*2654435761u))|1u;runtime_.setupRenewed=millis();
    }
    if(action=="apply" || action=="pause") {
      runtime_.setupSaving=false;
      if(runtime_.setupRestartRequired)restart_.request(millis());
    }
    writeSetup(output);return;
  }
  if(path=="/api/network" && method=="POST") {
    NetworkConfig next=network_.credentials().network;uint32_t choice=0;
    if(web_.hasArg("choice")) {
      if(!hardwareUnsigned(web_.value("choice"),choice)||choice>4 || (choice==4&&!ETHERNET_SUPPORTED)){reply(400,"invalid_network_choice");return;}
      next.choice=NetworkChoice(choice);
    }
    const char *prefixes[]={"wifi","eth"};IpConfig *interfaces[]={&next.wifi,&next.ethernet};
    for(unsigned i=0;i<2;++i) {
      const std::string prefix=prefixes[i];auto &ip=*interfaces[i];
      const auto mode=web_.value(prefix+"Mode");
      if(!mode.empty()) {if(mode!="dhcp"&&mode!="static"){reply(400,"invalid_ip_mode");return;}ip.automatic=mode=="dhcp";}
      const char *fields[]={"Address","Mask","Gateway","Dns1","Dns2"};uint32_t *values[]={&ip.address,&ip.mask,&ip.gateway,&ip.dns1,&ip.dns2};
      for(unsigned f=0;f<5;++f)if(web_.hasArg(prefix+fields[f])) {
        const auto value=web_.value(prefix+fields[f]);
        if(value.empty()&&f>=2)*values[f]=0;
        else if(!parseIpv4(value,*values[f])){reply(400,"invalid_ipv4");return;}
      }
      if(const auto error=ip.validate()){reply(400,error);return;}
    }
    auto hud=runtime_.hud.config;
    hud.wifi=next.choice==NetworkChoice::Ap || next.choice==NetworkChoice::SharedHost?HudWifi::Host:
        next.choice==NetworkChoice::SharedClient?HudWifi::Client:HudWifi::Existing;
    runtime_.controller.stopNow(-1,millis());hardware_.applyOutputs();
    if(!storage_.saveNetwork(next)){reply(503,"network_storage_failed");return;}
    if(!storage_.saveHudConfig(hud)){storage_.saveNetwork(network_.credentials().network);reply(503,"network_storage_failed");return;}
    invalidateSetup(SetupStep::Network);
    runtime_.controller.stopNow(-1,millis());hardware_.applyOutputs();
    network_.credentials().network=next;runtime_.hud.config.wifi=hud.wifi;
    reply(200,"");configurationRestart(setupBatch);return;
  }
  if(path=="/api/motion/coast") {
    auto &c=runtime_.controller;auto &m=c.coast;
    if(method=="POST") {
      unsigned axis=0,direction=1;uint32_t travel=10;
      if(!integerArg(web_.arg("axis"),axis,1)){reply(400,"invalid_axis");return;}
      const auto action=web_.value("action");Result result=Result::Invalid;
      if(action=="cancel"){c.cancelCoast(millis());result=Result::Ok;}
      else if(action=="start") {
        if(!integerArg(web_.arg("direction"),direction,1)||!hardwareUnsigned(web_.value("travel"),travel)){reply(400,"invalid_measurement");return;}
        result=c.startCoast(axis,direction?1:-1,float(travel),millis());
      } else if(action=="apply") {
        const auto old=c.speeds[axis];result=c.applyCoast(axis,millis());
        if(result==Result::Ok && !storage_.saveForkSettings(runtime_)){c.speeds[axis]=old;reply(503,"motion_storage_failed");return;}
        if(result==Result::Ok && !command && lease && lease==runtime_.setupLease && runtime_.setup.required && !runtime_.setup.paused){auto next=runtime_.setup;next.checked|=uint16_t(1u<<(12+axis));++next.generation;
          runtime_.setupStorageOk=storage_.saveSetup(next);if(runtime_.setupStorageOk)runtime_.setup=next;}
      }
      hardware_.applyOutputs();
      if(result!=Result::Ok){reply(409,Controller::resultName(result));return;}
    } else if(method!="GET"){reply(405,"method_not_allowed");return;}
    const char *phases[]={"idle","running","coasting","complete","failed","cancelled"};
    output << "{\"phase\":" << jsonQuoted(phases[m.phase]) << ",\"axis\":" << m.axis << ",\"reason\":" << jsonQuoted(m.reason) << ",\"results\":[";
    for(unsigned a=0;a<2;++a){if(a)output << ',';output << "{\"measured\":" << unsigned(m.measured[a]) << ",\"degrees\":[" << m.degrees[a][0] << ',' << m.degrees[a][1]
      << "],\"seconds\":[" << m.seconds[a][0] << ',' << m.seconds[a][1] << "]}";}
    output << "]}";return;
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
        << ",\"speedOutputPercent\":" << a.speedOutputPercent
        << ",\"speedSetpoint\":";output.decimal(c.runtimeSpeed(i),-1) << ",\"speedLimit\":";
      output.decimal(c.runtimeSpeedLimit(i),-1) << ",\"speedAdjustable\":" << (c.runtimeSpeedAvailable(i)?"true":"false");
      writeAxisMotion(output,c,i);output << '}';return;
    }
    output << "{\"model\":" << jsonQuoted(modelName(c.model))
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
      << ",\"commandPort\":" << runtime_.loggerSettings.commandPort;
    writeHardwareTrial(output,runtime_);
    output << ",\"axes\":[";
    for(unsigned i=0;i<2;++i) {
      if(i)output << ',';
      const auto &a=c.axes[i];
      output << "{\"current\":" << a.current << ",\"target\":" << a.target
        << ",\"calibrated\":" << (a.calibrated?"true":"false") << ",\"output\":" << a.speedOutputPercent << '}';
    }
    output << "]}";return;
  }
  if(path=="/api/diagnostics" && method=="GET") {
    if(web_.hasArg("previousFailure")) {
      unsigned i;if(!integerArg(web_.arg("previousFailure"),i,1)){reply(400,"invalid_failure");return;}
      writePreviousAllocation(output,i);return;
    }
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
    const auto writeTiming=[&](unsigned i) {
      const auto &t=runtime_.controller.motionTiming(i);
      const auto &f=runtime_.controller.motionFeedback(i);
      output << "{\"steps\":" << t.steps << ",\"maxIntervalMs\":" << t.maxIntervalMs
        << ",\"lateSteps\":" << t.lateSteps << ",\"sampleCount\":" << hardware_.sampleCount(i)
        << ",\"sampleMaxIntervalMs\":" << hardware_.sampleMaxIntervalMs(i)
        << ",\"feedbackRejected\":" << f.rejectedTotal << ",\"feedbackInvalid\":" << f.invalidEvents
        << ",\"rejectedAt\":" << f.rejectedAt << ",\"rejectedIntervalMs\":" << f.rejectedIntervalMs
        << ",\"rejectedPosition\":" << f.rejectedPosition << ",\"lastGoodPosition\":" << f.lastGoodPosition << '}';
    };
    if(web_.hasArg("axis")) {
      const auto axis=web_.arg("axis");
      if(axis!="az" && axis!="el") {reply(400,"invalid_axis");return;}
      // One live UART page: counters change between paginated requests.
      output << "{\"axis\":" << jsonQuoted(axis.c_str())
        << ",\"controlPeriodMs\":" << runtime_.controller.controlPeriodMs() << ",\"timing\":";
      writeTiming(axis=="az"?0:1);output << '}';return;
    }
    output << "{\"uptimeMs\":" << millis() << ",\"heapFree\":" << heapFreeBytes() << ",\"heapMin\":" << heapMinBytes()
      << ",\"heapMaxBlock\":" << heapMaxBlockBytes() << ",\"allocFailures\":" << runtimeMetrics.allocationFailures.load()
      << ",\"maxFailedAllocation\":" << runtimeMetrics.largestFailedAllocation.load()
      << ",\"networkMemoryLimited\":" << (networkLowMemory()?"true":"false")
      << ",\"udpDropped\":" << network_.udpDropped() << ",\"tcpAborts\":" << network_.tcpAborts()
      << ",\"loopMaxUs\":" << runtimeMetrics.loopMaxUs << ",\"loopsOver20ms\":" << runtimeMetrics.loopsOver20ms
      << ",\"webEnabled\":" << (WEB_INTERFACE_ENABLED?"true":"false") << ",\"bluetoothSupported\":" << (BLUETOOTH_SUPPORT?"true":"false")
      << ",\"psramBytes\":" << heap_caps_get_total_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)
      << ",\"controlPeriodMs\":" << runtime_.controller.controlPeriodMs() << ",\"motionTiming\":[";
    for (unsigned i=0;i<2;++i) {
      if(i) output << ',';
      writeTiming(i);
    }
    output << ']';
#if GRAPHICAL_DISPLAY
    output << ",\"panel\":{\"renderMaxUs\":" << panelMetrics.renderMaxUs
      << ",\"touchReads\":" << panelMetrics.touchReads << ",\"touchErrors\":" << panelMetrics.touchErrors
      << ",\"touchHealthy\":" << (panelMetrics.touchHealthy?"true":"false")
      << ",\"touchDown\":" << (panelMetrics.touchDown?"true":"false")
      << ",\"x\":" << panelMetrics.touchX << ",\"y\":" << panelMetrics.touchY << '}';
#endif
    output << '}';return;
  }
  if(path=="/api/network" && method=="GET") {
    const bool connected=radioWifi.status()==WL_CONNECTED;
    const String sta=radioWifi.localIP().toString(),ap=radioWifi.softAPIP().toString();
    output << "{\"wifiMode\":" << unsigned(radioWifi.getMode()) << ",\"wifiStatus\":" << int(radioWifi.status()) << ",\"wifiConnecting\":" << (network_.wifiConnecting()?"true":"false") << ",\"connected\":" << (connected?"true":"false")
      << ",\"ssid\":" << jsonQuoted(network_.credentials().wifiSsid.c_str())
      << ",\"staIp\":" << jsonQuoted(sta.c_str()) << ",\"apIp\":" << jsonQuoted(ap.c_str())
      << ",\"apAlwaysOn\":" << (network_.apAlwaysOn()?"true":"false") << ",\"rssi\":";
    if(connected)output << radioWifi.RSSI();else output << "null";
    output << ",\"ethernetSupported\":" << (ETHERNET_SUPPORTED?"true":"false")
      << ",\"ethStarted\":" << (network_.ethernetStarted()?"true":"false")
      << ",\"ethLink\":" << (ethernetLinkUp()?"true":"false")
      << ",\"ethConnected\":" << (ethernetConnected()?"true":"false")
      << ",\"ethIp\":" << jsonQuoted(ethernetIP().toString().c_str());
    const auto &nc=network_.credentials().network;
    output << ",\"choice\":" << unsigned(nc.choice);
    const char *names[]={"wifi","eth"};const IpConfig *ips[]={&nc.wifi,&nc.ethernet};
    for(unsigned i=0;i<2;++i){const auto &ip=*ips[i];char a[16],m[16],g[16],d1[16],d2[16];formatIpv4(ip.address,a);formatIpv4(ip.mask,m);formatIpv4(ip.gateway,g);formatIpv4(ip.dns1,d1);formatIpv4(ip.dns2,d2);
      output << ',' << jsonQuoted(names[i]) << ":{\"mode\":" << jsonQuoted(ip.automatic?"dhcp":"static") << ",\"address\":" << jsonQuoted(a)
        << ",\"mask\":" << jsonQuoted(m) << ",\"gateway\":" << jsonQuoted(g) << ",\"dns1\":" << jsonQuoted(d1) << ",\"dns2\":" << jsonQuoted(d2) << '}';}
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
    int azRaw = 0, azMv = 0, elRaw = 0, elMv = 0, azPhysicalMv = 0, elPhysicalMv = 0;
    hardware_.positionDiagRead(0, azRaw, azMv, &azPhysicalMv);
    if (runtime_.controller.model == forkrot::Model::G5500) {
      hardware_.positionDiagRead(1, elRaw, elMv, &elPhysicalMv);
    }
    String j = String("{\"hasEl\":") +
               (runtime_.controller.model == forkrot::Model::G5500 ? "true" : "false") +
               ",\"azRaw\":" + String(azRaw) + ",\"azMv\":" + (runtime_.controller.hardware.sensorType(0)?String("null"):String(azPhysicalMv)) +
               ",\"elRaw\":" + String(elRaw) + ",\"elMv\":" + (runtime_.controller.hardware.sensorType(1)?String("null"):String(elPhysicalMv)) +
               ",\"azValue\":" + String(azMv) + ",\"elValue\":" + String(elMv) +
               ",\"azSensor\":\"" + String(sensorName(runtime_.controller.hardware.sensorType(0))) +
               "\",\"elSensor\":\"" + String(sensorName(runtime_.controller.hardware.sensorType(1))) + "\"}";
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
    if(ok)invalidateSetup(SetupStep::Calibration);
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
    if(ok)invalidateSetup(SetupStep::Calibration);
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
    if (runtime_.controller.model != forkrot::Model::G5500 && p.startsWith("el")) {
      web_.send(400, "text/plain", "NO EL AXIS");
      return;
    }
    int a = hardware_.positionValue(0), e = hardware_.positionValue(1);
    if ((p.startsWith("az") && a<0) || (p.startsWith("el") && e<0)) { web_.send(409,"text/plain","POSITION SENSOR UNAVAILABLE");return; }
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
    if (runtime_.controller.model != forkrot::Model::G5500 && (p == "O2" || p == "F2")) {
      web_.send(400, "text/plain", "NO EL AXIS");
      return;
    }
    if (p == "O" || p == "O2" || p == "F" || p == "F2") {
      actions_.captureGs232Cal(p);
      const bool saved = runtime_.controller.fault != "calibration_save_failed" && runtime_.controller.fault != "position_sensor_unavailable";
      web_.send(saved ? 200 : 503, "text/plain", saved ? "2-POINT SAVED" : "CALIBRATION SAVE FAILED OR SENSOR UNAVAILABLE");
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
    const bool saved = runtime_.controller.fault != "calibration_save_failed" && runtime_.controller.fault != "position_sensor_unavailable";
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
    runtime_.controller.stopNow(-1,millis());hardware_.applyOutputs();
    if(!storage_.saveApPassword(p)){web_.send(503,"text/plain","Не удалось сохранить пароль AP.");return;}
    web_.send(200, "text/plain", p.length() ? "AP password saved" : "AP set to open");
    configurationRestart(setupBatch);

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
    bool staOk = (radioWifi.status() == WL_CONNECTED);
    String staIp = staOk ? radioWifi.localIP().toString() : "-";
    const bool apEnabled = (radioWifi.getMode() & WIFI_AP) != 0;
    String apIp = apEnabled ? radioWifi.softAPIP().toString() : "-";
    const bool ethOk=ethernetConnected();
    const String ethIp=ethOk?ethernetIP().toString():"-";
    String ip = ethOk ? ethIp : staOk ? staIp : apIp;
    const char *mode = ethOk ? (apEnabled ? "Ethernet + AP" : "Ethernet") : staOk ? (apEnabled ? "AP + Wi-Fi Client" : "Wi-Fi Client") : apEnabled ? "Access Point" : "Disconnected";
    int rssi = staOk ? radioWifi.RSSI() : 0;
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
                     (runtime_.controller.model != Model::G5500 || runtime_.controller.axes[1].calibrated)
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
      << ",\"azSensor\":" << jsonQuoted(sensorName(runtime_.controller.hardware.sensorType(0)))
      << ",\"elSensor\":" << jsonQuoted(sensorName(runtime_.controller.hardware.sensorType(1)))
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
      << ",\"ethernetSupported\":" << (ETHERNET_SUPPORTED?"true":"false")
      << ",\"ethStarted\":" << (network_.ethernetStarted()?"true":"false")
      << ",\"ethLink\":" << (ethernetLinkUp()?"true":"false")
      << ",\"ethConnected\":" << (ethOk?"true":"false")
      << ",\"ethIp\":" << jsonQuoted(ethIp.c_str())
      << ",\"connectedSsid\":" << jsonQuoted(staOk ? radioWifi.SSID().c_str() : "")
      << ",\"wifiConnecting\":" << (network_.wifiConnecting() ? "true" : "false")
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
      << ",\"networkMemoryLimited\":" << (networkLowMemory()?"true":"false")
      << ",\"telemetryIntervalMs\":" << (networkLowMemory()?500:WebPositionIntervalMs)
      << ",\"udpDropped\":" << network_.udpDropped() << ",\"tcpAborts\":" << network_.tcpAborts()
      << ",\"maxFailedAllocation\":" << runtimeMetrics.largestFailedAllocation.load()
      << ",\"loopMaxUs\":" << runtimeMetrics.loopMaxUs << ",\"loopsOver20ms\":" << runtimeMetrics.loopsOver20ms;
    const uint32_t feedbackNow = millis();
    j << ",\"localFeedback\":[";
    for (unsigned i=0; i<2; ++i) {
      if(i) j << ',';
      const bool available = !runtime_.hud.isHud() && !runtime_.controller.hardware.simulation &&
          (i==0 || runtime_.controller.model==Model::G5500) && hardware_.sampleMaxIntervalMs(i)>0;
      if(!available) j << "null";
      else j << "{\"ageMs\":" << uint32_t(feedbackNow-hardware_.sampledAt(i))
             << ",\"maxIntervalMs\":" << hardware_.sampleMaxIntervalMs(i) << '}';
    }
    j << ']';
    runtime_.hud.writeStatus(j);
    writeHardwareTrial(j,runtime_);
    if (!writeStateJson(j, runtime_.controller, runtime_.protocol, runtime_.loggerSettings, millis(), true)) {
      web_.send(503, "application/json", "{\"error\":\"status_too_large\"}");
      return;
    }
    web_.finish(200,"application/json");

    return;
  }

#if MQTT_SUPPORT
  if(path=="/api/mqtt" && (method=="GET" || method=="POST")) {
    auto config=runtime_.mqtt;
    if(runtime_.setup.required && !runtime_.setup.paused && runtime_.setupRestartRequired && !storage_.loadMqtt(config)){reply(503,"mqtt_storage_failed");return;}
    if(method=="POST") {
      auto next=config;
      bool invalid=false;
      for(size_t i=0;i<arguments.count;++i) {
        const auto &e=arguments.entries[i];
        for(size_t k=0;k<i;++k)if(arguments.entries[k].first==e.first)invalid=true;
        if(e.first=="setupToken")continue;
        if(e.first=="enabled" || e.first=="discovery") {
          const bool valid=e.second=="true" || e.second=="false" || e.second=="1" || e.second=="0";
          if(!valid)invalid=true;
          else (e.first=="enabled"?next.enabled:next.discovery)=e.second=="true" || e.second=="1";
        } else if(e.first=="host")invalid|=!mqttText(e.second,next.host);
        else if(e.first=="username")invalid|=!mqttText(e.second,next.username);
        else if(e.first=="password")invalid|=!mqttText(e.second,next.password);
        else if(e.first=="discoveryPrefix")invalid|=!mqttText(e.second,next.discoveryPrefix);
        else if(e.first=="port") {
          uint32_t value=0;if(!hardwareUnsigned(e.second,value) || !value || value>65535)invalid=true;else next.port=uint16_t(value);
        } else invalid=true;
      }
      if(invalid || next.validate()){reply(400,invalid?"invalid_mqtt_config":next.validate());return;}
      if(!HOME_ASSISTANT_DISCOVERY_SUPPORT && web_.hasArg("discovery") && next.discovery){reply(501,"feature_disabled");return;}
      const bool changed=!(next==config) || !runtime_.mqttStorageOk;
      if(changed) {
        runtime_.controller.stopNow(-1,millis());hardware_.applyOutputs();
        if(!storage_.saveMqtt(next)){reply(503,"mqtt_storage_failed");return;}
        // Keep the running client's credentials immutable until the safe restart.
        configurationRestart(setupBatch);
      }
      output << "{\"ok\":true,\"restart\":" << (changed?"true":"false") << '}';return;
    }
    output << "{\"enabled\":" << (config.enabled?"true":"false") << ",\"host\":" << jsonQuoted(config.host)
      << ",\"port\":" << config.port << ",\"username\":" << jsonQuoted(config.username)
      << ",\"hasPassword\":" << (config.password[0]?"true":"false") << ",\"discovery\":"
      << (config.discovery && HOME_ASSISTANT_DISCOVERY_SUPPORT?"true":"false") << ",\"discoveryPrefix\":" << jsonQuoted(config.discoveryPrefix)
      << ",\"storageOk\":" << (runtime_.mqttStorageOk?"true":"false") << ',';
    if(mqtt_)mqtt_->writeStatus(output);else output << "\"supported\":true,\"connected\":false,\"error\":\"not_started\"";
    output << '}';return;
  }
#endif
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
    if (web_.hasArg("simulationEnabled")) {
      for (auto entry : arguments)
        if (entry.first != "simulationEnabled" && entry.first != "setupToken") { reply(400,"invalid"); return; }
      auto &hud = runtime_.hud;
      const auto value = web_.arg("simulationEnabled");
      if (!hud.isHud()) { reply(409,"hud_only"); return; }
      if (value != "0" && value != "1") { reply(400,"invalid"); return; }
      const bool enabled = value == "1", previous = hud.config.simulationEnabled;
      if (enabled && !hud.setSimulationEnabled(true)) { reply(507,"storage_full"); return; }
      auto next = hud.config;
      next.simulationEnabled = enabled;
      if (!enabled && next.selected == HUD_SIMULATION_ID) next.selected = 0;
      if (!storage_.saveHudConfig(next)) {
        if (enabled && !previous) hud.setSimulationEnabled(false);
        reply(507,"storage_full"); return;
      }
      if (!enabled) hud.setSimulationEnabled(false);
      actions_.invalidateDisplay();
      reply(200,"ok"); return;
    }
    auto config = runtime_.hud.config;
    const String role = web_.arg("role"), wifi = web_.arg("wifi"), uart = web_.arg("uart");
    if((!HUD_SUPPORT && role=="hud") || (!HUD_UART_SUPPORT && uart=="1") ||
        (!WIFI_SUPPORT && (wifi=="host" || wifi=="client"))) {reply(501,"feature_disabled");return;}
    unsigned port = 0;
    if ((role != "hud" && role != "controller") ||
        (wifi != "existing" && wifi != "host" && wifi != "client") || (uart != "0" && uart != "1") ||
        !integerArg(web_.arg("port"), port) ||
        !network_.hudPortValid(port, runtime_.loggerSettings.n1mmPort, runtime_.loggerSettings.pstPort,
                               runtime_.loggerSettings.reportPort)) {
      web_.send(400, "application/json", "{\"ok\":false,\"result\":\"invalid_or_port_conflict\"}");
      return;
    }
    if (uart == "1" && board::validateHardware(runtime_.controller.hardware,true,role=="hud")) {
      web_.send(400, "application/json", "{\"ok\":false,\"result\":\"uart_disabled\"}");
      return;
    }
    config.role = role == "hud" ? DeviceRole::Hud : DeviceRole::Controller;
    config.wifi = wifi == "host" ? HudWifi::Host : wifi == "client" ? HudWifi::Client : HudWifi::Existing;
    config.uart = uart == "1";
    config.port = uint16_t(port);
    const auto &line=runtime_.controller.hardware.hudRs485()?runtime_.modbus.uart1:runtime_.modbus.uart0;
    config.link.baud=line.baud;config.link.frameBits=line.frameBits();
    // Old clients can echo the effective baud, but line edits belong to the
    // hardware endpoint, with its token, generation and verified persistence.
    if(web_.hasArg("baud")) {
      unsigned baud=0;
      if(!integerArg(web_.arg("baud"),baud)||baud!=line.baud) {reply(400,"uart_settings_in_hardware");return;}
    }
    unsigned interval=config.link.intervalMs,ready=unsigned(config.link.readyHigh);
    if((web_.hasArg("intervalMs")&&!integerArg(web_.arg("intervalMs"),interval,10000)) ||
       (web_.hasArg("readyHigh")&&!integerArg(web_.arg("readyHigh"),ready,1))) {reply(400,"invalid_uart_settings");return;}
    config.link.intervalMs=uint16_t(interval);config.link.readyHigh=ready;
    if(!config.link.valid()) {reply(400,"invalid_uart_settings");return;}
    if(const auto error=runtime_.modbus.wiring(runtime_.controller.hardware,config.uart)){reply(400,error);return;}
    const auto &previous=runtime_.hud.config;
    if(config.role==previous.role && config.wifi==previous.wifi && config.uart==previous.uart && config.port==previous.port &&
        config.link.intervalMs==previous.link.intervalMs && config.link.readyHigh==previous.link.readyHigh) {
      reply(200,"");return;
    }
    if (!storage_.saveHudConfig(config)) {
      web_.send(507, "application/json", "{\"ok\":false,\"result\":\"storage_full\"}");
      return;
    }
    if(config.role!=previous.role)invalidateSetup(SetupStep::Role);
    // Stop local outputs before rebooting into another role. Do not alter remote numerical jobs.
    if (!runtime_.hud.isHud()) {
      runtime_.controller.stopNow(-1, millis());
      hardware_.applyOutputs();
      runtime_.controller.driveFault = true;
    }
    web_.send(200, "application/json", "{\"ok\":true,\"result\":\"restart\"}");
    configurationRestart(setupBatch);

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
    const bool rs485=web_.arg("transport")=="rs485";
    endpoint.uart = rs485 || web_.arg("transport") == "uart";
    if (endpoint.uart && !hud.config.uart) {
      web_.send(400, "application/json", "{\"ok\":false,\"result\":\"uart_disabled\"}");
      return;
    }
    if(endpoint.uart && runtime_.modbus.bus) {
      unsigned first=1,last=247;
      if((web_.hasArg("first")&&!integerArg(web_.arg("first"),first,247))||
         (web_.hasArg("last")&&!integerArg(web_.arg("last"),last,247))||!first||first>last) {
        web_.send(400,"application/json","{\"ok\":false,\"result\":\"invalid_address\"}");return;
      }
      const bool ok=hud.discoverRs485(first,last,runtime_.modbus.first,runtime_.modbus.count);
      web_.send(ok?200:503,"application/json",ok?"{\"ok\":true,\"result\":\"searching\"}":"{\"ok\":false,\"result\":\"unavailable\"}");return;
    }
    if(rs485){web_.send(400,"application/json","{\"ok\":false,\"result\":\"modbus_bus_disabled\"}");return;}
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
    if (p->simulated()) {
      web_.send(400, "application/json", "{\"ok\":false,\"result\":\"invalid_peer\"}");
      return;
    }
    unsigned background = p->background;
    if (web_.hasArg("background") &&
        (!integerArg(web_.arg("background"), background) || !validHudBackground(background))) {
      web_.send(400, "application/json", "{\"ok\":false,\"result\":\"invalid_background\"}");
      return;
    }
    const bool oldSaved = p->saved;
    const auto oldBackground = p->background;
    char oldLabel[49];
    std::memcpy(oldLabel, p->label, sizeof oldLabel);
    if (web_.arg("action") == "remove")
      p->saved = false;
    else {
      const auto label = web_.arg("name");
      if (!hud.add(id, p->endpoint, label.length() ? label.c_str() : oldSaved ? oldLabel : p->state.name, true)) {
        web_.send(400, "application/json", "{\"ok\":false,\"result\":\"invalid_name\"}");
        return;
      }
      p->background = uint8_t(background);
    }
    if (!storage_.saveHudPeers(hud)) {
      p->saved = oldSaved;
      p->background = oldBackground;
      std::memcpy(p->label, oldLabel, sizeof oldLabel);
      web_.send(507, "application/json", "{\"ok\":false,\"result\":\"storage_full\"}");
      return;
    }
    bool selectionSaved = true;
    if (!p->saved) {
      hud.remove(id);
      selectionSaved = storage_.saveHudConfig(hud.config);
    } else if (!oldSaved && !hud.config.selected) {
      hud.select(id);
      selectionSaved = storage_.saveHudConfig(hud.config);
    }
    if (!selectionSaved) {
      hud.catalogError = true;
      web_.send(507, "application/json", "{\"ok\":false,\"result\":\"storage_full\"}");
      return;
    }
    actions_.invalidateDisplay();
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
  if(path=="/api/wifi" && (method=="GET" || method=="POST")) {
    if (runtime_.hud.config.wifi == HudWifi::Host && !setupBatch) {
      web_.send(409, "text/plain; charset=utf-8", "Для подключения к роутеру выберите обычный режим Wi-Fi в разделе «Пульт и связь».");
      return;
    }
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
                            "Не удалось сохранить настройки Wi-Fi. Подключение не изменено.";
      web_.send(invalid ? 400 : 500, "text/plain; charset=utf-8", message);
      return;
    }
    diagnostics_.dbgPrintln("[WIFI NVS] credentials saved and verified; connecting with setup AP retained");
    if(setupBatch)configurationRestart(setupBatch);
    else network_.connectWifi(ssid, password);
    web_.send(200, "text/plain; charset=utf-8", "Настройки Wi-Fi сохранены. Подключаемся к сети; полученный IP появится на этой странице.");
    return;
  }
  if(method=="POST" && (path=="/api/mode" || path=="/api/execute" || path=="/api/preset" || path=="/api/preset/run" || path=="/api/settings" || path=="/api/motion/reset" || path=="/api/motion/autotune" || path=="/api/speed")) {
      auto oldModel = runtime_.controller.model;
      auto oldSettings = runtime_.loggerSettings;
      const bool oldSpeedEnabled=runtime_.controller.speedEnabled;
      actions_.updatePosition();
      auto response = callApi(path,arguments);
      if (response.status == 200) {
        // Model/PWM changes use a logical STOP; resolve it before any NVS work.
        // Per-axis tuning/frequency changes already use stopNow(). Unrelated
        // settings preserve motion and the other axis keeps its current output.
        if(oldModel!=runtime_.controller.model || oldSpeedEnabled!=runtime_.controller.speedEnabled)
          runtime_.controller.stopNow(-1,millis());
        hardware_.applyOutputs();
        if (oldModel != runtime_.controller.model) {
          invalidateSetup(SetupStep::Model);
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
