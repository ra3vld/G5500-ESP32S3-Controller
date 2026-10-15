#pragma once
#include "Arduino.h"
#include <deque>
inline constexpr int SERIAL_8N1 = 0;
struct FakeUart {
  int number = -1, rxPin = -1, txPin = -1;
  uint32_t baud = 0;
  std::deque<unsigned char> rx;
  std::string tx;
};
inline FakeUart fakeUart;
class HardwareSerial {
public:
  explicit HardwareSerial(int number) { fakeUart.number = number; }
  void begin(uint32_t baud, int, int rx, int tx) { fakeUart.baud = baud; fakeUart.rxPin = rx; fakeUart.txPin = tx; }
  int available() { return fakeUart.rx.size(); }
  int read() { int byte = fakeUart.rx.front(); fakeUart.rx.pop_front(); return byte; }
  size_t write(const uint8_t *data, size_t size) { fakeUart.tx.append(reinterpret_cast<const char *>(data), size); return size; }
  size_t print(const String &reply) { fakeUart.tx += reply.c_str(); return reply.length(); }
};
