// Read-only I2C angle acquisition. No sensor configuration or OTP writes. GPL-3.0.
#pragma once
#include "position_sensor_config.hpp"
#include <cstddef>
namespace forkrot {
template<class Bus> bool encoderRegister(Bus &bus, uint8_t address, uint8_t reg, uint8_t *data, size_t size) {
  bus.beginTransmission(address);
  if(bus.write(reg)!=1 || bus.endTransmission(false)!=0) return false;
  if(bus.requestFrom(address,size,true)!=size) return false;
  for(size_t i=0;i<size;++i) {
    const int value=bus.read();
    if(value<0) return false;
    data[i]=uint8_t(value);
  }
  return true;
}
template<class Bus> bool readEncoder(Bus &bus, unsigned type, uint8_t address, uint16_t &raw) {
  uint8_t data[2];
  if(type==unsigned(PositionSensor::MT6701)) {
    // Follow the documented two single-byte reads, high register first
    // (not an assumed auto-increment).
    if(!encoderRegister(bus,address,0x03,data,1) || !encoderRegister(bus,address,0x04,data+1,1)) return false;
    raw=(uint16_t(data[0])<<6)|(data[1]>>2);
    return true;
  }
  if(type==unsigned(PositionSensor::AS5600)) {
    uint8_t status;
    if(!encoderRegister(bus,address,0x0b,&status,1) || (status&0x38)!=0x20 ||
        !encoderRegister(bus,address,0x0c,data,2) || (data[0]&0xf0)) return false;
    raw=(uint16_t(data[0])<<8)|data[1];
    return true;
  }
  return false;
}
}
