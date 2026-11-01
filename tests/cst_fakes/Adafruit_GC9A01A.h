#pragma once
#include <Arduino.h>
struct SPIClass { void begin(int,int,int,int) {} };
inline SPIClass SPI;
inline void delay(unsigned ms) {fakeHardware.now+=ms;}
class Adafruit_GC9A01A {
public:
  Adafruit_GC9A01A(SPIClass *,int,int,int) {}
  void begin(unsigned) {}
  void setRotation(unsigned) {}
  void invertDisplay(bool) {}
  void sendCommand(uint8_t,const uint8_t *,unsigned) {}
  void fillScreen(unsigned) {}
  void startWrite() {}
  void endWrite() {}
  void setAddrWindow(int,int,unsigned,unsigned) {}
  void writePixels(uint16_t *,unsigned) {}
};
