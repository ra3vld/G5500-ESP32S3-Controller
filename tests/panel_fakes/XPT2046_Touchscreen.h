#pragma once
#include <Arduino.h>
struct TS_Point {int x=0,y=0,z=500;};
class XPT2046_Touchscreen {
public:
  bool down=false;
  TS_Point point;
  bool touched() const {return down;}
  TS_Point getPoint() const {return point;}
};
inline void delay(unsigned ms) {fakeHardware.now+=ms;}
inline long map(long value,long lo,long hi,long outLo,long outHi) {
  return outLo+(value-lo)*(outHi-outLo)/(hi-lo);
}
