#include "live.hpp"
#include "motion_json.hpp"
#include <cstring>
#include <cstdio>
#include <limits>
namespace forkrot {
namespace {
int hex(char c) { return c >= '0' && c <= '9' ? c-'0' : c >= 'a' && c <= 'f' ? c-'a'+10 : c >= 'A' && c <= 'F' ? c-'A'+10 : -1; }
bool identity(std::string_view s, uint64_t &n) {
  if (s.empty() || s.size()>16) return false;
  n=0; for(char c:s) { int h=hex(c); if(h<0)return false; n=(n<<4)|unsigned(h); } return n!=0;
}
}
void liveBootText(char (&out)[17], uint64_t boot) { std::snprintf(out,sizeof out,"%016llx",static_cast<unsigned long long>(boot)); }
std::string_view LiveRequest::get(std::string_view key) const { for(size_t i=0;i<count;++i)if(args[i].first==key)return args[i].second;return {}; }
bool LiveRequest::parse(std::string_view text) {
  count=0; id=selection=0; boot=0; path={};
  if(text.empty() || text.size()>LIVE_INPUT_CAPACITY)return false;
  size_t used=0;
  while(!text.empty()) {
    if(count==16)return false;
    const auto amp=text.find('&'); auto pair=text.substr(0,amp); const auto equal=pair.find('=');
    if(equal==pair.npos || equal==0)return false;
    std::string_view values[2];
    for(unsigned part=0;part<2;++part) {
      auto raw=part?pair.substr(equal+1):pair.substr(0,equal); const size_t start=used;
      for(size_t i=0;i<raw.size();++i) {
        unsigned char c=raw[i];
        if(c=='%') { if(i+2>=raw.size()||hex(raw[i+1])<0||hex(raw[i+2])<0)return false; c=unsigned(hex(raw[i+1])*16+hex(raw[i+2]));i+=2; }
        else if(c=='+')c=' ';
        if(c<32||c==127)return false;
        bytes[used++]=char(c);
      }
      values[part]={bytes+start,used-start};
    }
    for(size_t i=0;i<count;++i)if(args[i].first==values[0])return false;
    args[count++]={values[0],values[1]};
    if(amp==text.npos)break;
    text.remove_prefix(amp+1);
  }
  path=get("path");
  const bool allowed=path=="/api/target"||path=="/api/speed"||path=="/api/move"||path=="/api/mode"||path=="/api/execute"||path=="/api/preset/run"||path=="/api/hud/select";
  return allowed && get("v")=="1" && requestGeneration(get("cid"),id) && id && identity(get("boot"),boot) && requestGeneration(get("selection"),selection);
}
Response executeLive(const LiveRequest &r, Controller &c, Protocol &p, Settings &s, Hud &h, uint32_t now) {
  if(r.boot!=h.boot || r.selection!=h.selectionEpoch)return {409,"{\"ok\":false,\"result\":\"cancelled\"}"};
  if(r.path=="/api/hud/select") {
    uint64_t node=0;
    const bool ok=h.isHud()&&identity(r.get("id"),node)&&h.select(node);
    if(ok)h.selectionDirty=true;
    return {ok?200:400,ok?"{\"ok\":true,\"result\":\"ok\"}":"{\"ok\":false,\"result\":\"invalid\"}"};
  }
  auto result=apiView(c,p,s,r.path,{r.args,r.count},now);
  c.tick(now);
  if(result.status==200&&r.path=="/api/mode"&&!h.isHud())h.settingsDirty=true;
  return result;
}
namespace {
// Typed hashing avoids float-to-text conversion, scratch JSON and a second CRC
// pass. Hash fields explicitly: object padding/pointers are never inspected.
class LiveHash {
  uint32_t hash_=2166136261u;
  void byte(uint8_t value) { hash_=(hash_^value)*16777619u; }
public:
  uint32_t fingerprint() const { return hash_; }
  LiveHash &operator<<(std::string_view s) {
    *this << uint64_t(s.size());for(unsigned char c:s)byte(c);return *this;
  }
  LiveHash &operator<<(const char *s) { return *this << std::string_view(s); }
  LiveHash &operator<<(const std::string &s) { return *this << std::string_view(s); }
  LiveHash &operator<<(JsonQuoted s) { return *this << s.text; }
  template<class T,std::enable_if_t<std::is_integral_v<T>,int> = 0>
  LiveHash &operator<<(T value) {
    auto n=uint64_t(value);for(unsigned i=0;i<8;++i){byte(uint8_t(n));n>>=8;}return *this;
  }
  LiveHash &operator<<(float value) {
    uint32_t bits;std::memcpy(&bits,&value,sizeof bits);return *this << bits;
  }
  LiveHash &decimal(float value,int) { return *this << value; }
};
// Exactly the same projection feeds the cheap change detector and rare JSON.
// Fast fields belong only to the independent binary snapshot below.
template<class Writer> void metadata(Writer &j,const Controller &c,const Hud &h) {
  const auto boolean=[](bool b){return b?"true":"false";};
  j << "{\"targetValid\":" << boolean(c.targetKnown) << ",\"controlMode\":" << jsonQuoted(Controller::modeName(c.mode))
    << ",\"pendingValid\":" << boolean(c.pendingValid)
    << ",\"pending\":{\"az\":" << c.pending.az << ",\"el\":" << c.pending.el << ",\"hasAz\":" << boolean(c.pending.hasAz) << ",\"hasEl\":" << boolean(c.pending.hasEl) << '}'
    << ",\"lastTarget\":{\"az\":" << c.lastTarget.az << ",\"el\":" << c.lastTarget.el << ",\"hasAz\":" << boolean(c.lastTarget.hasAz) << ",\"hasEl\":" << boolean(c.lastTarget.hasEl) << '}'
    << ",\"stopEpoch\":" << c.stopEpoch << ",\"presetActive\":" << boolean(c.presetActive) << ",\"presetSlot\":" << c.presetSlot
    << ",\"model\":" << jsonQuoted(modelName(c.model)) << ",\"azMode\":" << unsigned(c.azProfile)
    << ",\"azProfile\":" << jsonQuoted(azProfileName(c.azProfile)) << ",\"azStart\":" << c.minimum
    << ",\"azSpan\":" << c.azRange.span() << ",\"azMax\":" << c.maximum << ",\"azRangeRevision\":" << c.rangeRevision
    << ",\"elProfile\":" << jsonQuoted(elProfileName(c.elProfile)) << ",\"elStart\":" << c.elRange.start
    << ",\"elSpan\":" << c.elRange.span() << ",\"elMax\":" << c.elRange.end << ",\"elRangeRevision\":" << c.rangeRevision
    << ",\"selectedCalOk\":" << boolean(c.axes[0].calibrated&&(c.model!=Model::G5500||c.axes[1].calibrated))
    << ",\"fault\":" << jsonQuoted(c.fault) << ",\"axes\":[";
  for(unsigned i=0;i<2;++i) {
    if(i)j << ',';
    const auto &m=reportedMotion(c,i);
    j << "{\"regulatorMode\":" << jsonQuoted(regulatorName(m.mode)) << ",\"motionReady\":" << boolean(m.ready)
      << ",\"holdEnabled\":" << boolean(m.mode==RegulatorMode::Cascade&&m.holdEnabled) << '}';
  }
  const auto &t=c.autoTune;
  j << "],\"autotune\":{\"phase\":" << jsonQuoted(Controller::autoTunePhaseName(t.phase))
    << ",\"active\":" << boolean(t.active()) << ",\"id\":" << t.id << ",\"axis\":" << t.axis
    << ",\"mode\":" << jsonQuoted(regulatorName(t.mode)) << ",\"direction\":" << t.direction << ",\"reason\":" << jsonQuoted(t.reason);
  if(t.active())j << ",\"stages\":" << t.options.stages;
  const unsigned cases=t.options.trackingCases(),first=t.options.has(TuneSlow)?0:4;
  if(t.active()&&t.trackingPeriodMs&&!t.trackingVerified)
    j << ",\"trackingTrial\":" << std::min(3*cases,t.trackingCandidate*cases+t.trackingCase-first+1)
      << ",\"trackingTrials\":" << 3*cases << ",\"trackingPeriodMs\":" << ((t.trackingCase/2)%2?1500:200)
      << ",\"trackingDurationMs\":" << t.trackingDurationMs[t.trackingCase/2]
      << ",\"trackingTravel\":" << (t.trackingCase<4?t.trackingSlow:t.trackingFast)*t.trackingDurationMs[t.trackingCase/2]/1000;
  j << '}';
#if HUD_SUPPORT
  char node[17],selected[17];
  std::snprintf(node,sizeof node,"%012llx",static_cast<unsigned long long>(h.id));
  std::snprintf(selected,sizeof selected,"%012llx",static_cast<unsigned long long>(h.config.selected));
  const auto *p=h.active();
  j << ",\"deviceRole\":" << jsonQuoted(h.isHud()?"hud":"controller") << ",\"deviceId\":" << jsonQuoted(node)
    << ",\"hudPort\":" << h.config.port << ",\"hudUart\":" << boolean(h.config.uart) << ",\"hudWifi\":" << unsigned(h.config.wifi)
    << ",\"hudBaud\":" << h.config.link.baud << ",\"hudIntervalMs\":" << h.config.link.intervalMs
    << ",\"hudReadyHigh\":" << unsigned(h.config.link.readyHigh) << ",\"hudSimulationEnabled\":" << boolean(h.config.simulationEnabled)
    << ",\"hudSelected\":" << jsonQuoted(selected) << ",\"hudSelectionEpoch\":" << h.selectionEpoch;
  if(p&&!p->simulated()&&!p->endpoint.uart&&!p->endpoint.mqtt) {
    const auto a=p->endpoint.address;
    j << ",\"hudHost\":\"" << (a&255) << '.' << ((a>>8)&255) << '.' << ((a>>16)&255) << '.' << (a>>24) << '"';
  }
  j << ",\"hudName\":" << jsonQuoted(p?p->label:"")
    << ",\"hudTransport\":" << jsonQuoted(p?(p->simulated()?"simulation":p->endpoint.mqtt?"mqtt":p->endpoint.uart?"uart":"wifi"):"")
    << ",\"hudPeers\":" << h.count() << ",\"remoteDrive\":";
  if(p&&p->received)j << jsonQuoted(p->state.drive==0?"YAESU":p->state.drive==1?"SEPARATE_PWM":p->state.drive==2?"BTS7960":p->state.drive==3?"SIMULATED":"NONE");
  else j << "null";
  j << ",\"remoteBoard\":";
  if(p&&p->received)j << unsigned(p->state.board);else j << "null";
  j << ",\"hudCatalogError\":" << boolean(h.catalogError);
#else
  (void)h;
#endif
  j << '}';
}
void put32(char *p,uint32_t value) {
  for(unsigned i=0;i<4;++i){p[i]=char(value);value>>=8;}
}
void putFloat(char *p,float value) {
  static_assert(sizeof(float)==4&&std::numeric_limits<float>::is_iec559,"binary live requires IEEE754 float32");
  uint32_t bits;std::memcpy(&bits,&value,sizeof bits);put32(p,bits);
}
}
uint32_t liveMetadataFingerprint(const Controller &c,const Hud &h) {
  LiveHash hash;metadata(hash,c,h);return hash.fingerprint();
}
bool writeLiveMetadata(JsonWriter &j,const Controller &c,const Hud &h) {
  metadata(j,c,h);return j.good();
}
void writeLiveBinary(char *data,const Controller &c,const Hud &h,uint32_t now,
                     uint32_t revision,uint32_t metadataRevision,bool memoryLimited) {
  std::memset(data,0,LIVE_BINARY_SIZE);
  data[0]='G';data[1]='L';data[2]=2;data[3]=1;
  put32(data+4,revision);put32(data+8,metadataRevision);
  put32(data+12,uint32_t(h.boot));put32(data+16,uint32_t(h.boot>>32));
  put32(data+20,h.selectionEpoch);put32(data+24,(c.remaining(now)/1000)*1000);
  const auto *peer=h.active();const bool hudLive=peer&&peer->live&&uint32_t(h.now-peer->liveAt)<1000;
  put32(data+28,unsigned(c.moving())|(unsigned(memoryLimited)<<1)|(unsigned(h.available())<<2)|(unsigned(hudLive)<<3));
  const auto age=h.freshnessAge();put32(data+32,age==UINT32_MAX?age:(age/250)*250);
  const uint32_t counters[]={h.rejected,h.timeouts,h.busy,h.received,h.sent,h.sendErrors,uint32_t(h.sendErrno),h.commandAttempts,h.commandSendFailures};
  for(unsigned i=0;i<9;++i)put32(data+36+4*i,counters[i]);
  putFloat(data+72,c.autoTune.active()?c.axes[unsigned(c.autoTune.axis)].speedOutputPercent:0);
  putFloat(data+76,c.autoTune.validationError);
  for(unsigned i=0;i<2;++i) {
    const auto &a=c.axes[i];char *p=data+80+40*i;
    const float values[]={a.current,a.target,a.speedOutputPercent,a.measuredVelocity,a.requestedVelocity,a.profileAcceleration,
                          c.runtimeSpeed(i),c.runtimeSpeedLimit(i),c.effectiveTolerance(i)};
    for(unsigned n=0;n<9;++n)putFloat(p+4*n,values[n]);
    p[36]=char(a.direction);
    p[37]=char(unsigned(a.tracking)|(unsigned(a.completed)<<1)|(unsigned(a.calibrated)<<2)|(unsigned(a.holdArmed)<<3)|
               (unsigned(c.variableSpeed(i))<<4)|(unsigned(c.runtimeSpeedAvailable(i))<<5));
    p[38]=char(a.motionPhase);p[39]=char(a.motionError);
  }
}
uint32_t liveControlFingerprint(const Controller &c,const Hud &h) {
  LiveHash j;
  j << c.rangeRevision << ',' << c.minimum << ',' << c.maximum << ',' << unsigned(c.mode) << ',' << c.stopEpoch << ',' << c.pendingValid << ',' << c.pending.az << ',' << c.pending.el
    << ',' << c.presetSlot << ',' << c.presetActive << ',' << c.targetKnown << ',' << h.selectionEpoch << ',' << h.available() << ',' << c.fault;
  for(const auto &a:c.axes)j<<','<<a.target<<','<<a.direction<<','<<a.wanted<<','<<a.tracking<<','<<a.completed<<','<<a.calibrated;
  for(unsigned i=0;i<2;++i) {
    j<<','<<unsigned(c.motionSettings[i].mode)<<','<<c.motionSettings[i].ready
    <<','<<unsigned(c.axes[i].motionPhase)<<','<<unsigned(c.axes[i].motionError)<<','<<c.axes[i].holdArmed
    <<',';
    j.decimal(c.runtimeSpeed(i),-1);
  }
  j<<','<<unsigned(c.autoTune.phase)<<','<<c.autoTune.id<<','<<c.autoTune.direction<<','<<c.autoTune.level;
  return j.fingerprint();
}
bool writeLiveState(JsonWriter &j,const Controller &c,const Hud &h,uint32_t now) {
  const auto boolean=[](bool b){return b?"true":"false";};
  j << "{\"az\":" << c.axes[0].current << ",\"el\":" << c.axes[1].current
    << ",\"targetAz\":" << c.axes[0].target << ",\"targetEl\":" << c.axes[1].target
    << ",\"targetValid\":" << boolean(c.targetKnown) << ",\"controlMode\":" << jsonQuoted(Controller::modeName(c.mode))
    << ",\"remainingMs\":" << (c.remaining(now)/1000)*1000 << ",\"pendingValid\":" << boolean(c.pendingValid)
    << ",\"pending\":{\"az\":" << c.pending.az << ",\"el\":" << c.pending.el << ",\"hasAz\":" << boolean(c.pending.hasAz) << ",\"hasEl\":" << boolean(c.pending.hasEl) << '}'
    << ",\"lastTarget\":{\"az\":" << c.lastTarget.az << ",\"el\":" << c.lastTarget.el << ",\"hasAz\":" << boolean(c.lastTarget.hasAz) << ",\"hasEl\":" << boolean(c.lastTarget.hasEl) << '}'
    << ",\"stopEpoch\":" << c.stopEpoch
    << ",\"moving\":" << boolean(c.moving()) << ",\"motion\":" << jsonQuoted(c.moving()?"MOVING":"STOP")
    << ",\"presetActive\":" << boolean(c.presetActive) << ",\"presetSlot\":" << c.presetSlot
    << ",\"model\":" << jsonQuoted(modelName(c.model)) << ",\"azMode\":" << unsigned(c.azProfile)
    << ",\"azProfile\":" << jsonQuoted(azProfileName(c.azProfile))
    << ",\"azStart\":" << c.minimum << ",\"azSpan\":" << c.azRange.span()
    << ",\"azMax\":" << c.maximum << ",\"azRangeRevision\":" << c.rangeRevision
    << ",\"elProfile\":" << jsonQuoted(elProfileName(c.elProfile))
    << ",\"elStart\":" << c.elRange.start << ",\"elSpan\":" << c.elRange.span()
    << ",\"elMax\":" << c.elRange.end << ",\"elRangeRevision\":" << c.rangeRevision
    << ",\"selectedCalOk\":" << boolean(c.axes[0].calibrated&&(c.model!=Model::G5500||c.axes[1].calibrated))
    << ",\"fault\":" << jsonQuoted(c.fault) << ",\"axes\":[";
  for(unsigned i=0;i<2;++i) {
    if(i)j<<',';
    const auto &a=c.axes[i];
    j << "{\"current\":" << a.current << ",\"target\":" << a.target << ",\"direction\":" << a.direction
      << ",\"tracking\":" << boolean(a.tracking) << ",\"completed\":" << boolean(a.completed)
      << ",\"calibrated\":" << boolean(a.calibrated) << ",\"speedOutputPercent\":" << a.speedOutputPercent
      << ",\"speedEnabled\":" << boolean(c.variableSpeed(i)) << ",\"stopToleranceDegrees\":" << c.effectiveTolerance(i);
    j << ",\"speedSetpoint\":";j.decimal(c.runtimeSpeed(i),-1) << ",\"speedLimit\":";
    j.decimal(c.runtimeSpeedLimit(i),-1) << ",\"speedAdjustable\":" << boolean(c.runtimeSpeedAvailable(i));
    writeAxisMotion(j,c,i); j << '}';
  }
  j << "],\"azSpeedOutputPercent\":" << c.axes[0].speedOutputPercent << ",\"elSpeedOutputPercent\":" << c.axes[1].speedOutputPercent;
  j << ",\"autotune\":";writeAutoTune(j,c);
  h.writeStatus(j, true); j << '}'; return j.good();
}
}
