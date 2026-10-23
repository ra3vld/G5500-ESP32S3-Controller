// UART event receiver is independent of rendering and command execution. GPL-3.0.
#pragma once
#include "modbus_rtu.hpp"
namespace forkrot::mb {
class SerialPort {
  void *state_=nullptr;
public:
  SerialPort()=default;
  SerialPort(const SerialPort&)=delete;
  SerialPort &operator=(const SerialPort&)=delete;
#ifdef ARDUINO
  ~SerialPort();
  bool begin(unsigned port,int rx,int tx,int de,uint32_t baud);
  bool read(Frame &frame);
  bool send(const Frame &frame);
  bool idle() const;
  uint32_t activity() const;
  uint32_t rejected() const;
#else
  bool begin(unsigned,int,int,int,uint32_t){return false;}
  bool read(Frame &){return false;}
  bool send(const Frame &){return false;}
  bool idle() const{return false;}
  uint32_t activity() const{return 0;}
  uint32_t rejected() const{return 0;}
#endif
};
}
