// Bounded GPIO configuration documents shared by all API transports. GPL-3.0.
#pragma once
#include "board.hpp"
#include "json_writer.hpp"
namespace forkrot {
inline void writePinMap(JsonWriter &j,const PinMap &pins) {
  j << '{'; bool first=true;
  for(const auto &d:pinDefinitions) {
    if(!first) j << ',';
    first=false; j << jsonQuoted(d.name) << ':' << int(pins.*d.member);
  }
  j << '}';
}
inline void writePinCapabilities(JsonWriter &j,bool hudUart) {
  const PinMap defaults;
  j << "{\"displayKind\":" << DISPLAY_KIND << ",\"localDriveElectrical\":" << unsigned(CROWPANEL_LOCAL_DRIVE)
    << ",\"hudUart\":" << unsigned(hudUart) << ",\"roles\":[";
  bool first=true;
  for(const auto &d:pinDefinitions) {
    if(!first) j << ',';
    first=false;
    j << "{\"name\":" << jsonQuoted(d.name) << ",\"group\":" << jsonQuoted(pinGroupName(d.group))
      << ",\"kind\":" << jsonQuoted(pinKindName(d.kind)) << ",\"optional\":" << unsigned(d.optional)
      << ",\"default\":" << int(defaults.*d.member) << '}';
  }
  j << "],\"gpios\":[";
  for(int pin=0;pin<=48;++pin) {
    if(pin) j << ',';
    const unsigned caps=unsigned(board::gpio(pin)) | (unsigned(board::output(pin))<<1) |
        (unsigned(board::adc(pin))<<2) | (unsigned(board::pullup(pin))<<3);
    const auto *owner=board::unavailablePinOwner(pin);
    j << "{\"gpio\":" << pin << ",\"caps\":" << caps << ",\"owner\":" << jsonQuoted(owner?owner:"") << '}';
  }
  j << "],\"buttonDefault\":" << USER_BUTTON_GPIO << '}';
}
}
