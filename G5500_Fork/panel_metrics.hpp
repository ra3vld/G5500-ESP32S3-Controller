// Display/input diagnostics, read on the same loop as the panel. GPL-3.0.
#pragma once
#include <cstdint>
#include <array>
namespace forkrot {
struct PanelMetrics {
  struct TouchEvent {
    uint32_t at=0,gesture=0;
    uint16_t x=0,y=0;
    uint8_t flags=0,mask=0,contact=0;
    int8_t az=0,el=0;
  };
  std::array<TouchEvent,8> touchEvents{};
  uint32_t touchEventsWritten=0;
  bool rawDown=false,rawHealthy=false;
  uint32_t traceGesture=0;
  int8_t traceAz=0,traceEl=0;
  void traceTouch(const TouchEvent &e) {
    const auto sign=[](int n){return (n>0)-(n<0);};
    const bool down=e.flags&1,healthy=e.flags&2,manual=e.flags&8;
    if(down!=rawDown || healthy!=rawHealthy || (manual && ((e.flags&16) || e.gesture!=traceGesture ||
        sign(e.az)!=sign(traceAz) || sign(e.el)!=sign(traceEl)))) {
      touchEvents[touchEventsWritten++%touchEvents.size()]=e;
    }
    rawDown=down;rawHealthy=healthy;
    if(manual) {traceGesture=e.gesture;traceAz=e.az;traceEl=e.el;}
  }
  uint32_t renderMaxUs=0,touchReads=0,touchErrors=0;
  uint32_t touchGapMaxMs=0,manualUnavailable=0,manualCancelled=0;
  uint8_t manualResult=0;
  bool touchHealthy=false,touchDown=false;
  uint16_t touchX=0,touchY=0;
};
inline PanelMetrics panelMetrics;
}
