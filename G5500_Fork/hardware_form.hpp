// G5500 Fork; GPL-3.0. Small settings path independent of WebServer's String parser.
#pragma once
#include "hardware_config.hpp"
#include <limits>
namespace forkrot {
struct HardwareForm {
  HardwareConfig config;
  uint32_t token = 0, generation = 0;
};
inline bool hardwareUnsigned(std::string_view text, uint32_t &out) {
  if (text.empty() || text.size() > 10) return false;
  uint32_t value = 0;
  for (char c : text) {
    if (c < '0' || c > '9' || value > (UINT32_MAX-unsigned(c-'0'))/10) return false;
    value = value*10+unsigned(c-'0');
  }
  out = value; return true;
}
namespace hardware_form_detail {
class Parser {
  HardwareForm next;
  bool settings;
  std::array<bool,11+pinDefinitions.size()+LocalSettings{}.size()+SensorSettings{}.size()> seen{};
  unsigned fields=0;
public:
  Parser(bool settings, const HardwareConfig &base):settings(settings) { next.config=base; }
  bool add(std::string_view name, std::string_view text) {
    constexpr std::string_view names[] = {"token","generation","driveInterface","simulation","bluetoothEnabled",
      "bluetoothMode","forceApSta","buttonGpio","buttonActiveLow","buttonPullup","pwmInverted"};
    unsigned key = 0;
    while (key < 11 && names[key] != name) ++key;
    const auto *pin=pinDefinition(name);
    unsigned local=0;
    while(local<11 && name!=localSettingNames[local]) ++local;
    unsigned sensor=0;
    while(sensor<SensorSettings{}.size() && name!=sensorSettingNames[sensor]) ++sensor;
    if(key==11) {
      if(pin) key=11+unsigned(pin-pinDefinitions.data());
      else if(local<11) key=11+pinDefinitions.size()+local;
      else if(sensor<SensorSettings{}.size()) key=11+pinDefinitions.size()+LocalSettings{}.size()+sensor;
      else return false;
    }
    if ((!settings && key > 1) || seen[key]) return false;
    seen[key]=true; ++fields;
    if(sensor<SensorSettings{}.size()) {
      uint32_t value=0;
      if(!hardwareUnsigned(text,value) || value>16383) return false;
      next.config.sensors[sensor]=uint16_t(value);
      return true;
    }
    if(local<11) {
      uint32_t value=0;
      if(!hardwareUnsigned(text,value) || value>4095) return false;
      next.config.local[local]=uint16_t(value);
      return true;
    }
    if(pin) {
      uint32_t value=0;
      if(text=="-1") next.config.pins.*pin->member=-1;
      else if(!hardwareUnsigned(text,value) || value>48) return false;
      else next.config.pins.*pin->member=int8_t(value);
      return true;
    }
    uint32_t value = 0;
    if (key == 7 && text == "-1") next.config.buttonGpio = -1;
    else {
      if (!hardwareUnsigned(text,value)) return false;
      switch (key) {
      case 0: next.token=value; break;
      case 1: next.generation=value; break;
      case 2: if (value>2) return false; next.config.drive=value; break;
      case 5: if (value<1 || value>3) return false; next.config.bluetoothMode=value; break;
      case 7: if (value>48) return false; next.config.buttonGpio=value; break;
      default:
        if (value>1) return false;
        if (key==3) next.config.simulation=value;
        if (key==4) next.config.bluetooth=value;
        if (key==6) next.config.forceAp=value;
        if (key==8) next.config.buttonLow=value;
        if (key==9) next.config.buttonPullup=value;
        if (key==10) next.config.pwmInverted=value;
      }
    }
    return true;
  }
  bool finish(HardwareForm &out) const {
    if (!seen[0] || !seen[1] || (settings ? fields==2 : fields!=2) || !next.token || !next.generation) return false;
    out=next; return true;
  }
};
}
inline bool parseHardwareForm(std::string_view body, HardwareForm &out, bool settings, const HardwareConfig &base = HardwareConfig{}) {
  if (body.empty() || body.size() > 1536) return false;
  hardware_form_detail::Parser parser(settings,base);
  for (;;) {
    const auto end=body.find('&');
    const auto part=body.substr(0,end);
    const auto equal=part.find('=');
    if(equal==part.npos || !parser.add(part.substr(0,equal),part.substr(equal+1)))return false;
    if(end==body.npos)break;
    body.remove_prefix(end+1);
  }
  return parser.finish(out);
}
// HTTP and wire transports already decoded these fields. Re-encoding the form
// here used another 1537-byte buffer on the nested controller-loop stack.
template<class Arguments>
inline bool parseHardwareArguments(const Arguments &arguments, HardwareForm &out, bool settings,
                                   const HardwareConfig &base = HardwareConfig{}) {
  hardware_form_detail::Parser parser(settings,base);
  size_t bytes=0;
  for(auto entry:arguments) {
    if(entry.first.size()>1536 || entry.second.size()>1536 ||
       entry.first.find_first_of("&=")!=entry.first.npos || entry.second.find_first_of("&=")!=entry.second.npos)return false;
    bytes+=(bytes?1:0)+entry.first.size()+1+entry.second.size();
    if(bytes>1536 || !parser.add(entry.first,entry.second))return false;
  }
  return parser.finish(out);
}
} // namespace forkrot
