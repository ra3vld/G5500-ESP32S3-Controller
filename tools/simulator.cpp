#include "api.hpp"
#include "motion_json.hpp"
#include "calibration.hpp"
#include "live.hpp"
#include "input.hpp"
#include "hud.hpp"
#include "hud_input.hpp"
#include "datagram_socket.hpp"
#include <iostream>
#include <sstream>
#include <chrono>
#include <cmath>
using namespace forkrot;
class SimHudTransport : public HudTransport {
public:
  DatagramSocket socket;
  bool sendHud(const HudEndpoint &e, const uint8_t *p, size_t n) override {
    return !e.uart && socket.send(e.address, e.port, {reinterpret_cast<const char *>(p), n});
  }
  void receive(Hud &hud, uint32_t now) {
    uint8_t data[HUD_FRAME_MAX];
    for (unsigned i = 0; i < 16; ++i) {
      uint32_t address;
      uint16_t port;
      int n = socket.receiveInto(data, sizeof data, address, port);
      if (n == -1)
        break;
      if (n > 0)
        hud.receive({address, port, false}, data, size_t(n), now);
    }
  }
};
int main(){
  Controller c;
  for(auto &m:c.motionSettings)m.ready=true;
  Protocol p(c);
  Calibration calibration(c);
  calibration.state().az45OSet=calibration.state().az45FSet=calibration.state().az45LinearValid=true;
  calibration.state().az36OSet=calibration.state().az36FSet=calibration.state().az36LinearValid=true;
  std::array<CalibrationState,2> calProfiles{{calibration.state(),calibration.state()}};
  Settings cfg;
  Hud hud(c, p);
  SimHudTransport link;
  hud.setTransport(&link);
  std::array<std::array<Preset, 4>, 2> profiles{{c.presets, c.presets}};
  std::array<std::array<MotionSettings,2>,2> motionProfiles{{c.motionSettings,c.motionSettings}};
  std::array<std::array<SpeedSettings,2>,2> speedProfiles{{c.speeds,c.speeds}};
  for (auto &preset : profiles[1])
    preset.target.hasEl = false;
  c.feedback(0, 0, true, true);
  hud.begin(1, uint64_t(std::chrono::steady_clock::now().time_since_epoch().count()));
  uint32_t now = 0;
  float az = 0, el = 0;
  Button button;
  HudKnob knob(hud,c);
  std::string line;
  while (std::getline(std::cin, line)) {
    std::string reply;
    Response response;
    std::string kind;
    auto split = line.find(' ');
    kind = line.substr(0, split);
    auto args = split == std::string::npos ? "" : line.substr(split + 1);
    if (kind == "live_state") {
      char data[LIVE_STATE_CAPACITY]; JsonWriter writer(data,sizeof data);
      writeLiveState(writer,c,hud,now); char boot[17];liveBootText(boot,hud.boot);
      char meta[LIVE_STATE_CAPACITY],binary[LIVE_BINARY_SIZE];JsonWriter metadata(meta,sizeof meta);
      writeLiveMetadata(metadata,c,hud);writeLiveBinary(binary,c,hud,now,0,0,false);
      std::string encoded;for(unsigned char byte:binary){encoded+="0123456789abcdef"[byte>>4];encoded+="0123456789abcdef"[byte&15];}
      response.body="{\"boot\":"+jsonQuote(boot)+",\"control\":"+std::to_string(liveControlFingerprint(c,hud))+
        ",\"metadataHash\":"+std::to_string(liveMetadataFingerprint(c,hud))+",\"metadata\":"+std::string(metadata.data(),metadata.size())+
        ",\"binary\":"+jsonQuote(encoded)+",\"state\":"+std::string(writer.data(),writer.size())+"}";
    } else if (kind == "live") {
      LiveRequest request;
      response=request.parse(args)?executeLive(request,c,p,cfg,hud,now):Response{400,"{\"ok\":false,\"result\":\"invalid\"}"};
    } else if (kind == "hud_init") {
      auto form = decodeForm(args);
      hud.config.role = form["role"] == "hud" ? DeviceRole::Hud : DeviceRole::Controller;
      hud.config.port = uint16_t(std::stoul(form["port"]));
      hud.begin(std::stoull(form["id"], nullptr, 16),
                uint64_t(std::chrono::steady_clock::now().time_since_epoch().count()));
      if (!link.socket.begin(hud.config.port))
        response = {503, "{\"ok\":false}"};
    } else if (kind == "step") {
      unsigned ms = std::stoul(args);
      if (hud.isHud()) {
        now += ms;
        ms = 0;
        hud.service(now);
        link.receive(hud, now);
        az = c.axes[0].current;
        el = c.axes[1].current;
      }

      // Integrate the speed ramp at 20 ms even for accelerated simulation time.
      while (ms) {
        unsigned slice = (c.variableSpeed(0) || c.variableSpeed(1)) ? std::min(ms, 20u) : ms;
        float seconds = slice / 1000.f;
        const float azSpeed = c.variableSpeed(0) ? c.axes[0].speedOutputPercent / 100.f : 1.f;
        az = c.azRange.clamp(az + c.axes[0].direction * 6.f * seconds * azSpeed);
        const float elSpeed = c.variableSpeed(1) ? c.axes[1].speedOutputPercent / 100.f : 1.f;
        el = c.elRange.clamp(el + c.axes[1].direction * 6.f * seconds * elSpeed);
        if (c.axes[0].tracking) {
          if (c.axes[0].direction > 0)
            az = std::min(az, c.axes[0].target);
          else if (c.axes[0].direction < 0)
            az = std::max(az, c.axes[0].target);
        }
        if (c.axes[1].tracking) {
          if (c.axes[1].direction > 0)
            el = std::min(el, c.axes[1].target);
          else if (c.axes[1].direction < 0)
            el = std::max(el, c.axes[1].target);
        }
        now += slice;
        c.feedbackAt(0,az,true,now);c.feedbackAt(1,el,true,now);
        c.tick(now);
        ms -= slice;
      }
    } else if (kind == "udp" || kind == "line") {
      auto packet = decodeForm(args)["packet"];
      auto result = kind == "udp" ? p.datagram(packet, now) : p.line(packet, now, true);
      response.status = result.result == Result::Deferred  ? 202
                        : result.result == Result::Invalid ? 400
                                                           : 200;
      reply = result.reply;
      response.body = std::string("{\"result\":") + jsonQuote(Controller::resultName(result.result)) +
                      ",\"matched\":" + (result.matched ? "true" : "false") +
                      ",\"requestId\":" + std::to_string(hud.requestId()) + "}";
    } else if (kind == "encoder") {
      knob.sample(true,false,std::stoi(args),now,false);
      response.body=std::string("{\"result\":")+jsonQuote(Controller::resultName(knob.lastResult))+
                    ",\"target\":"+std::to_string(hud.intendedTarget(0,c.axes[0].current))+
                    ",\"requestId\":"+std::to_string(hud.requestId())+"}";
    } else if (kind == "button") {
      if (button.sample(args == "released", now))
        c.execute(now);
    } else if (kind == "http") {
      auto pos = args.find(' ');
      std::string path = args.substr(0, pos);
      auto form = decodeForm(pos == std::string::npos ? "" : args.substr(pos + 1));
      auto old = c.model;
      auto oldIndex = old == Model::G5500 ? 0 : 1;
      profiles[oldIndex] = c.presets;
      motionProfiles[oldIndex]=c.motionSettings;speedProfiles[oldIndex]=c.speeds;
      calProfiles[oldIndex] = calibration.state();
      if(path=="/api/motion") {
        char data[4096];JsonWriter j(data,sizeof data);writeMotionConfiguration(j,c);response.body=j.data();
      } else if (path == "/api/device" && form.count("simulationEnabled")) {
        const auto value = form["simulationEnabled"];
        const bool ok = (value == "0" || value == "1") && hud.setSimulationEnabled(value == "1");
        response = {ok ? 200 : 400, ok ? "{\"ok\":true,\"result\":\"ok\"}" : "{\"ok\":false,\"result\":\"invalid\"}"};
      } else if (path == "/api/hud/peers" && !form.count("id")) {
        char data[4096];
        JsonWriter j(data, sizeof data);
        hud.writePeers(j, form.count("offset") ? std::stoul(form["offset"]) : 0, 8,
                       form["discovered"] == "1");
        response.body = j.data();
      } else if (path == "/api/hud/discover") {
        in_addr address{};
        const auto ip = form["address"].empty() ? "255.255.255.255" : form["address"].c_str();
        if (inet_pton(AF_INET, ip, &address) != 1)
          response.status = 400;
        else
          hud.discover({address.s_addr,
                        uint16_t(form["port"].empty() ? hud.config.port : std::stoul(form["port"])), false});
        response.body = "{\"ok\":true,\"result\":\"searching\"}";
      } else if (path == "/api/hud/peers") {
        const uint64_t id = std::stoull(form["id"], nullptr, 16);
        auto *peer = hud.peer(id);
        const auto background = form.find("background");
        const bool validBackground = background == form.end() ||
            (background->second.size() == 1 && background->second[0] >= '0' &&
             background->second[0] <= '9' && validHudBackground(unsigned(background->second[0] - '0')));
        if (!peer)
          response.status = 404;
        else if (peer->simulated() || !validBackground)
          response.status = 400;
        else if (form["action"] == "remove")
          hud.remove(id);
        else {
          const bool wasSaved = peer->saved;
          const std::string name = form["name"].empty() ? (peer->saved ? peer->label : peer->state.name) : form["name"];
          hud.add(id, peer->endpoint, name.c_str(), true);
          if (background != form.end()) peer->background = uint8_t(background->second[0] - '0');
          if (!wasSaved && !hud.config.selected)
            hud.select(id);
        }
        response.body = response.status == 200 ? "{\"ok\":true,\"result\":\"ok\"}" : "{\"ok\":false}";
      } else if (path == "/api/hud/select") {
        const bool ok = hud.select(std::stoull(form["id"], nullptr, 16));
        response = {ok ? 200 : 400, ok ? "{\"ok\":true,\"result\":\"ok\"}" : "{\"ok\":false}"};
      } else if (path == "/api/hud/command") {
        const auto r = hud.result(uint32_t(std::stoul(form["id"])));
        response = {r.result == Result::Deferred ? 202 : 200,
                    std::string("{\"requestId\":") + std::to_string(r.id) + ",\"ok\":" +
                        ((r.result == Result::Ok || r.result == Result::Pending) ? "true" : "false") +
                        ",\"result\":" + jsonQuote(Controller::resultName(r.result)) + "}"};
      } else if (path == "/api/azmode" || path == "/api/azrange") {
        const auto profile=path=="/api/azmode"?(form["m"]=="36"?"p36":form["m"]=="45"?"p45":"bad"):form["profile"];
        const uint8_t mode=profile=="p36"?AZ_P36:profile=="p45"?AZ_P45:AZ_CUSTOM;
        float start=0,span=mode==AZ_P36?360:450;
        bool ok=profile=="p36"||profile=="p45"||profile=="custom";
        if(mode==AZ_CUSTOM)ok=ok&&number(form["start"],start)&&number(form["span"],span);
        ok=ok&&!hud.isHud()&&calibration.configureAz(mode,start,span);
        if(ok){c.setRange(calibration.activeAzRange(),mode,now);az=c.azRange.clamp(az);c.feedback(az,el,true,true);}
        response={ok?200:400,ok?"{\"ok\":true}":"{\"error\":\"invalid_range\"}"};
      } else if(path=="/api/azcal") {
        if(form.count("op")) {
          float angle;const int mv=int(100+(az-c.minimum)*2700/c.azRange.span());
          const bool ok=number(form["angle"],angle)&&calibration.editAzPoint(form["op"].c_str(),angle,mv);
          response={ok?200:400,ok?"{\"ok\":true}":"{\"error\":\"invalid_point\"}"};
        } else {char data[2048];JsonWriter j(data,sizeof data);calibration.writeAzJson(j);response.body=j.data();}
      } else if(path=="/api/elrange") {
        const auto profile=form["profile"];const uint8_t mode=profile=="standard"?EL_STANDARD:EL_CUSTOM;
        float start=0,span=180;bool ok=profile=="standard"||profile=="custom";
        if(mode==EL_CUSTOM)ok=ok&&number(form["start"],start)&&number(form["span"],span);
        ok=ok&&!hud.isHud()&&calibration.configureEl(mode,start,span);
        if(ok){c.setElRange(calibration.activeElRange(),mode,now);el=c.elRange.clamp(el);c.feedback(az,el,true,true);}
        response={ok?200:400,ok?"{\"ok\":true}":"{\"error\":\"invalid_range\"}"};      } else if(path=="/api/elcal") {
        if(form.count("op")) {
          float angle;const int mv=int(100+(el-c.elRange.start)*2700/c.elRange.span());
          const bool ok=number(form["angle"],angle)&&calibration.editElPoint(form["op"].c_str(),angle,mv);
          response={ok?200:400,ok?"{\"ok\":true}":"{\"error\":\"invalid_point\"}"};
        } else {char data[2048];JsonWriter j(data,sizeof data);calibration.writeElJson(j);response.body=j.data();}
      } else if(path=="/api/calreset") {
        if(form["axis"]=="az")calibration.resetAz(form["mode"]=="multi");
        else calibration.resetEl(form["mode"]=="multi");
        response.body="{\"ok\":true}";
      } else if(path=="/api/calmode") {
        calibration.state().calibrationMode=form["m"]=="multi"?CAL_MULTI_POINT:CAL_GS232_2POINT;
        response.body="{\"ok\":true}";
      } else if(path=="/api/cal2") {
        const auto point = form["p"];
        if (point == "o" || point == "f") {
          calibration.captureAzEndpoint(point == "f", int(100+(az-c.minimum)*2700/c.azRange.span()));
        } else if ((point == "o2" || point == "f2") && c.model == Model::G5500) {
          calibration.captureElEndpoint(point == "f2", int(100+(el-c.elRange.start)*2700/c.elRange.span()));
        } else response.status = 400;
        response.body=response.status == 200 ? "{\"ok\":true}" : "{\"ok\":false}";
      } else if (path == "/api/cal") {
        const auto point = form["p"];
        auto &cal = calibration.state();
        bool captured = false;
        if (cal.gsAzMode != AZ_CUSTOM) {
          const unsigned count = cal.gsAzMode == AZ_P36 ? AZ36_CAL_N : AZ45_CAL_N;
          int *values = cal.gsAzMode == AZ_P36 ? cal.az36CalMv : cal.az45CalMv;
          for (unsigned i = 0; i < count; ++i) if (point == "az" + std::to_string(i * 90)) {
            values[i] = int(100+(az-c.minimum)*2700/c.azRange.span()); captured = true;
          }
        }
        if (c.model == Model::G5500 && cal.elProfile == EL_STANDARD) {
          for (unsigned i = 0; i < EL_CAL_N; ++i) if (point == "el" + std::to_string(i * 45)) {
            cal.elCalMv[i] = int(100+(el-c.elRange.start)*2700/c.elRange.span()); captured = true;
          }
        }
        calibration.refreshMultiCalValidity();
        response={captured?200:400,captured?"{\"ok\":true}":"{\"ok\":false}"};
      }
      else
        response = api(c, p, cfg, path, form, now);
      if (old != c.model) {
        const unsigned index=c.model==Model::G5500?0:1;
        c.motionSettings=motionProfiles[index];c.speeds=speedProfiles[index];
        if(path=="/api/settings") response=api(c,p,cfg,path,form,now);
        c.presets = profiles[index];calibration.state()=calProfiles[index];
        c.setRange(calibration.activeAzRange(),calibration.state().gsAzMode,now);az=c.azRange.clamp(az);
        c.setElRange(calibration.activeElRange(),calibration.state().elProfile,now);el=c.elRange.clamp(el);
        c.feedback(az, el, true, true);
      }
    }
    hud.service(now);
    link.receive(hud, now);
    c.tick(now);
    if (hud.isHud()) {
      az = c.axes[0].current;
      el = c.axes[1].current;
    }
    auto state = stateJson(c, p, cfg, now);
    state.pop_back();
    std::ostringstream extra;
    extra
        << ",\"az\":" << az << ",\"el\":" << (c.model == Model::G800DXA ? 0 : el)
        << ",\"targetAz\":" << c.axes[0].target << ",\"targetEl\":" << c.axes[1].target
        << ",\"targetValid\":" << (c.targetKnown ? "true" : "false")
        << ",\"azMode\":" << unsigned(c.azProfile) << ",\"directionAz\":" << c.axes[0].direction
        << ",\"directionEl\":" << c.axes[1].direction << ",\"wantedAz\":" << c.axes[0].wanted
        << ",\"calibrated\":true,\"selectedCalOk\":true,\"calMode\":" << jsonQuote(calibration.state().calibrationMode==CAL_MULTI_POINT?"multi":"2") << ",\"ip\":\"SIMULATOR\",\"wifiMode\":"
           "\"Виртуальное оборудование\",\"ssid\":\"\",\"rssi\":null,\"btConnected\":false,\"motion\":"
        << jsonQuote(c.moving() ? "MOVING" : "STOP") << ",\"uptimeSeconds\":" << now / 1000;
    extra << ",\"az2p0\":" << (calibration.activeAzOSet() ? calibration.activeAz2Zero() : -1)
          << ",\"az2pf\":" << (calibration.activeAzFSet() ? calibration.activeAz2Full() : -1)
          << ",\"el2p0\":" << (calibration.activeElOSet() ? calibration.activeEl2Zero() : -1)
          << ",\"el2pf\":" << (calibration.activeElFSet() ? calibration.activeEl2Full() : -1);
    const auto &cal = calibration.state();
    const bool p36 = cal.gsAzMode == AZ_P36;
    for (unsigned i = 0; i < (p36 ? AZ36_CAL_N : AZ45_CAL_N); ++i)
      extra << ",\"azp" << i * 90 << "\":" << (p36 ? cal.az36CalMv[i] : cal.az45CalMv[i]);
    for (unsigned i = 0; i < EL_CAL_N; ++i)
      extra << ",\"elp" << i * 45 << "\":" << cal.elCalMv[i];
    char hudJson[1536];
    JsonWriter hj(hudJson, sizeof hudJson);
    hud.writeStatus(hj);
    state += extra.str() + std::string(hj.data(), hj.size()) + "}";
    std::cout << "{\"status\":" << response.status << ",\"body\":" << jsonQuote(response.body)
              << ",\"reply\":" << jsonQuote(reply) << ",\"state\":" << state << "}" << std::endl;
  }
}
