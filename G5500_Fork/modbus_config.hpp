// Explicitly enabled serial protocols; existing installations default to legacy.
#pragma once
#include "hardware_config.hpp"
namespace forkrot {
struct ModbusConfig {
  uint8_t pc=0, bus=0, address=1, node=201, first=201, count=1;
  bool operator==(const ModbusConfig &b) const {
    return pc==b.pc && bus==b.bus && address==b.address && node==b.node && first==b.first && count==b.count;
  }
  const char *validate() const {
    if(pc>1 || bus>1 || address<1 || address>247 || first<1 || count<1 || count>4 ||
        unsigned(first)+count>248 || node<first || node>=unsigned(first)+count ||
        (address>=first && address<unsigned(first)+count)) return "invalid_modbus";
    return nullptr;
  }
  const char *wiring(const HardwareConfig &h,bool hudUart) const {
    if(auto error=validate())return error;
    if(pc && (!h.rs485() || h.pins.uartRx<0 || h.pins.uartTx<0 || (hudUart && !h.hudRs485())))return "modbus_pc_pins";
    if(bus && (!h.hudRs485() || !hudUart))return "modbus_bus_pins";
    return nullptr;
  }
};
using ModbusBytes=std::array<uint8_t,16>;
inline ModbusBytes encodeModbus(const ModbusConfig &c) {
  ModbusBytes b{'M','B',1,c.pc,c.bus,c.address,c.node,c.first,c.count};
  uint32_t crc=2166136261u;for(unsigned i=0;i<12;++i)crc=(crc^b[i])*16777619u;
  for(unsigned i=0;i<4;++i)b[12+i]=uint8_t(crc>>(8*i));
  return b;
}
inline bool decodeModbus(const ModbusBytes &b,ModbusConfig &c) {
  ModbusConfig next{b[3],b[4],b[5],b[6],b[7],b[8]};
  if(b[0]!='M'||b[1]!='B'||b[2]!=1||next.validate()||encodeModbus(next)!=b)return false;
  c=next;return true;
}
struct ModbusStatus { uint32_t received=0,rejected=0,timeouts=0,passes=0,recoveries=0;uint8_t owner=0;bool fault=false; };
}
