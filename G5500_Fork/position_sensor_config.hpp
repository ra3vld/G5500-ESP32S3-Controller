// G5500 Fork; GPL-3.0.
#pragma once
#include <array>
#include <cstdint>
namespace forkrot {
enum class PositionSensor : uint8_t { Potentiometer, MT6701, AS5600 };
// Per axis: type, 7-bit I2C address, software zero; then AZ/EL direction flags.
using SensorSettings = std::array<uint16_t,8>;
inline constexpr const char *sensorSettingNames[] = {
  "azSensor", "azSensorAddress", "azSensorZero", "elSensor", "elSensorAddress", "elSensorZero", "azSensorInverted", "elSensorInverted"};
inline constexpr const char *sensorName(unsigned type) {
  return type==1?"MT6701":type==2?"AS5600":"ADC";
}
inline constexpr unsigned sensorCounts(unsigned type) { return type==1?16384:type==2?4096:0; }
}
