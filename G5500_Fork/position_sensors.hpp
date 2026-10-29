// Runtime I2C buses for axis feedback, separate from CrowPanel's display bus. GPL-3.0.
#pragma once
#include "board.hpp"
#include "magnetic_encoder.hpp"
#include <Wire.h>
namespace forkrot {
class PositionSensors {
  TwoWire first_{DISPLAY_KIND==DISPLAY_CROWPANEL_21?1:0}, second_{FORK_C3 || DISPLAY_KIND==DISPLAY_ST7796 || DISPLAY_KIND==DISPLAY_ST77916?0:1};
  TwoWire *axes_[2]{};
  int raw_[2]{-1,-1};
  int previous_[2]{-1,-1};
public:
  void begin(const HardwareConfig &c) {
    axes_[0]=axes_[1]=nullptr;
    if(c.simulation) return;
    for(unsigned i=0;i<2;++i) {
      if(!c.sensorType(i) || (i && c.singleAxis())) continue;
      const int sda=i?c.pins.elSensorSda:c.pins.azSensorSda;
      const int scl=i?c.pins.elSensorScl:c.pins.azSensorScl;
#if DISPLAY_KIND == DISPLAY_GC9A01
      // The panel initializes Wire once. Do not remap/restart its touch bus.
      if(sda==board::PIN_I2C_SDA && scl==board::PIN_I2C_SCL) axes_[i]=&Wire;
      continue;
#endif
      if(i && axes_[0] && sda==c.pins.azSensorSda && scl==c.pins.azSensorScl) {
        axes_[i]=axes_[0];continue;
      }
      auto &bus=i && axes_[0]?second_:first_;
      bus.setTimeOut(2); // bounded failed transactions; no blocking retry loop
      if(bus.begin(sda,scl,400000)) axes_[i]=&bus;
    }
  }
  int read(const HardwareConfig &c,unsigned axis) {
    uint16_t raw;
    if(!axes_[axis] || !readEncoder(*axes_[axis],c.sensorType(axis),c.sensorAddress(axis),raw)) {
      raw_[axis]=-1;return -1;
    }
    raw_[axis]=raw;
    const unsigned counts=sensorCounts(c.sensorType(axis));
    const unsigned offset=(unsigned(raw)+counts-c.sensorZero(axis))%counts;
    const int value=c.sensorInverted(axis)?(counts-offset)%counts:offset;
    // A branch cut cannot be a real shaft step at rotator speeds. Reject it
    // before the median filter can turn it into a calibrated endpoint.
    if(previous_[axis]>=0 && unsigned(value>previous_[axis]?value-previous_[axis]:previous_[axis]-value)>counts/2)
      return -1;
    previous_[axis]=value;
    return value;
  }
  int raw(unsigned axis) const { return raw_[axis]; }
};
}
