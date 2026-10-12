#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

// Only the Arduino surface used by the storage and GPIO adapters is simulated.
class String {
public:
  String() = default;
  String(const char *value) : value_(value ? value : "") {}
  String(std::string value) : value_(std::move(value)) {}
  template <typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
  String(T value) {
    if constexpr (std::is_signed_v<T>) {
      value_ = std::to_string(static_cast<long long>(value));
    } else {
      value_ = std::to_string(static_cast<unsigned long long>(value));
    }
  }
  String(float value, unsigned digits = 2) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(digits) << value;
    value_ = stream.str();
  }
  const char *c_str() const { return value_.c_str(); }
  size_t length() const { return value_.size(); }
  String &operator+=(const String &other) { value_ += other.value_; return *this; }
  String &operator+=(char value) { value_ += value; return *this; }
  friend String operator+(String left, const String &right) { return left += right; }
  friend bool operator==(const String &left, const String &right) { return left.value_ == right.value_; }
  friend bool operator!=(const String &left, const String &right) { return !(left == right); }
private:
  std::string value_;
};

template <typename T> T constrain(T value, T minimum, T maximum) {
  return std::max(minimum, std::min(value, maximum));
}

inline constexpr int LOW = 0, HIGH = 1, OUTPUT = 1, INPUT_PULLUP = 2, ADC_11db = 3;
struct GpioEvent { std::string operation; int pin; uint32_t value; };
struct FakeHardware {
  uint32_t now = 0;
  bool attachSucceeds = true;
  std::map<int, int> levels, millivolts;
  std::vector<GpioEvent> events;
};
inline FakeHardware fakeHardware;
inline uint32_t millis() { return fakeHardware.now; }
inline void digitalWrite(int pin, int level) {
  fakeHardware.levels[pin] = level;
  fakeHardware.events.push_back({"write", pin, uint32_t(level)});
}
inline int digitalRead(int pin) { return fakeHardware.levels[pin]; }
inline void pinMode(int pin, int mode) { fakeHardware.events.push_back({"mode", pin, uint32_t(mode)}); }
inline int analogReadMilliVolts(int pin) { return fakeHardware.millivolts[pin]; }
inline int analogRead(int pin) { return fakeHardware.millivolts[pin]; }
inline void delayMicroseconds(unsigned) {}
inline void analogReadResolution(unsigned) {}
inline void analogSetPinAttenuation(int, int) {}
inline void rgbLedWrite(int, int, int, int) {}
inline bool ledcAttach(int pin, unsigned frequency, unsigned resolution) {
  fakeHardware.events.push_back({"attach", pin, frequency});
  return fakeHardware.attachSucceeds && resolution == 10;
}
inline void ledcWrite(int pin, uint32_t duty) { fakeHardware.events.push_back({"pwm", pin, duty}); }
inline void ledcDetach(int pin) { fakeHardware.events.push_back({"detach", pin, 0}); }
