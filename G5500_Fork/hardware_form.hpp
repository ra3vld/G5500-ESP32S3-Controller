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
inline bool parseHardwareForm(std::string_view body, HardwareForm &out, bool settings) {
  constexpr std::string_view names[] = {"token","generation","driveInterface","simulation","bluetoothEnabled",
    "bluetoothMode","forceApSta","buttonGpio","buttonActiveLow","buttonPullup","pwmInverted"};
  if (body.empty() || body.size() > 512) return false;
  HardwareForm next; unsigned seen = 0;
  for (;;) {
    const auto end = body.find('&');
    const auto part = body.substr(0,end);
    const auto equal = part.find('=');
    if (equal == part.npos) return false;
    const auto name = part.substr(0,equal), text = part.substr(equal+1);
    unsigned key = 0;
    while (key < 11 && names[key] != name) ++key;
    if (key == 11 || (!settings && key > 1) || (seen & (1u<<key))) return false;
    seen |= 1u<<key;
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
    if (end == body.npos) break;
    body.remove_prefix(end+1);
  }
  if (seen != (settings ? 2047u : 3u) || !next.token || !next.generation) return false;
  out=next; return true;
}
} // namespace forkrot
