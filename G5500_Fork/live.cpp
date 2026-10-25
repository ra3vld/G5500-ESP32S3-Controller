#include "live.hpp"
#include "motion_json.hpp"
#include <cstring>
#include <cstdio>
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
uint32_t liveControlFingerprint(const Controller &c,const Hud &h) {
  char data[768];JsonWriter j(data,sizeof data);
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
  return hudCrc(reinterpret_cast<const uint8_t *>(j.data()),j.size());
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
