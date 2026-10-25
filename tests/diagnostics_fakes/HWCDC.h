#pragma once
#include "Arduino.h"
#include <string>
struct HWCDC {
  bool started=false, consuming=false;
  uint32_t timeoutMs=100, waitedMs=0;
  std::string output;
  void begin() {started=true;}
  void setTxTimeoutMs(uint32_t timeout) {timeoutMs=timeout;}
  explicit operator bool() const {return started;}
  int available() const {return 0;}
  int read() const {return -1;}
  int availableForWrite() {if(!consuming)waitedMs+=timeoutMs;return consuming?256:0;}
  size_t write(const uint8_t *data,size_t count) {
    if(!consuming) {waitedMs+=timeoutMs;return 0;}
    output.append(reinterpret_cast<const char *>(data),count);return count;
  }
  size_t print(const String &text) {return write(reinterpret_cast<const uint8_t *>(text.c_str()),text.length());}
  size_t println(const String &text) {return print(text)+println();}
  size_t println() {return write(reinterpret_cast<const uint8_t *>("\n"),1);}
};
inline HWCDC fakeUsb;
#if ARDUINO_USB_MODE
inline HWCDC &HWCDCSerial=fakeUsb;
#else
inline HWCDC &Serial=fakeUsb;
#endif
