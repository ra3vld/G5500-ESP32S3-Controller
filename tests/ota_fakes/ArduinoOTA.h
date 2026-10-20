#pragma once
#include <cstdint>
#include <cstddef>
#include <functional>
#include <cstring>
#ifdef DEVICE_API_TEST
#include "Arduino.h"
inline auto &otaNow = fakeHardware.now;
#else
inline uint32_t otaNow = 0;
inline uint32_t millis() { return otaNow; }
#endif
inline constexpr int U_FLASH = 0, U_SPIFFS = 100;
struct FakeESP {
  uint32_t flash = 4 << 20, image = 1500000;
  uint32_t getFlashChipSize() const { return flash; }
  uint32_t getSketchSize() const { return image; }
  uint64_t getEfuseMac() const { return 0x123456; }
};
inline FakeESP ESP;
struct FakeUpdate {
  bool running = false, beginOk = true, writeOk = true, endOk = true;
  unsigned aborts = 0;
  uint32_t count = 0, size = 0;
  bool isRunning() const { return running; }
  bool begin(uint32_t n, int) { size = n; count = 0; return running = beginOk; }
  size_t write(uint8_t *, size_t n) { if (!writeOk) return 0; count += n; return n; }
  bool end() { running = false; return endOk && count == size; }
  void abort() { running = false; ++aborts; }
};
inline FakeUpdate Update;
enum ota_error_t { OTA_AUTH_ERROR, OTA_BEGIN_ERROR, OTA_CONNECT_ERROR, OTA_RECEIVE_ERROR, OTA_END_ERROR };
struct FakeArduinoOTA {
  bool enabled = false;
  int command = U_FLASH;
  std::function<void()> start, completed;
  std::function<void(ota_error_t)> error;
  std::function<void(unsigned, unsigned)> progress;
  FakeArduinoOTA &setHostname(const char *) { return *this; }
  FakeArduinoOTA &setPassword(const char *) { return *this; }
  FakeArduinoOTA &setRebootOnSuccess(bool) { return *this; }
  void setTimeout(int) {}
  void onStart(std::function<void()> f) { start = f; }
  void onEnd(std::function<void()> f) { completed = f; }
  void onError(std::function<void(ota_error_t)> f) { error = f; }
  void onProgress(std::function<void(unsigned, unsigned)> f) { progress = f; }
  void begin() { enabled = true; }
  void handle() {}
  int getCommand() { return command; }
  void end() { enabled = false; }
};
inline FakeArduinoOTA ArduinoOTA;
