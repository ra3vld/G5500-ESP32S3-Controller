// Local panel menu; remote values are limited to the negotiated HUD protocol. GPL-3.0.
#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
namespace forkrot {
struct PanelMenuView {
  bool open=false,connections=false,local=true,devices=false;
  bool brightnessAvailable=false,apAvailable=false,apOn=false;
  uint8_t brightness=100;
  uint32_t epoch=0;
  char device[49]{},info[384]{},notice[96]{},apLabel[64]{};
  bool operator==(const PanelMenuView &v) const {
    return open==v.open && connections==v.connections && local==v.local && devices==v.devices &&
      brightnessAvailable==v.brightnessAvailable && brightness==v.brightness && apAvailable==v.apAvailable &&
      apOn==v.apOn && epoch==v.epoch && !std::strcmp(device,v.device) && !std::strcmp(info,v.info) &&
      !std::strcmp(notice,v.notice) && !std::strcmp(apLabel,v.apLabel);
  }
};
inline void panelUptime(char *out,size_t size,uint64_t seconds) {
  std::snprintf(out,size,"%lluд %02u:%02u:%02u",static_cast<unsigned long long>(seconds/86400),
      unsigned(seconds/3600%24),unsigned(seconds/60%60),unsigned(seconds%60));
}
}
