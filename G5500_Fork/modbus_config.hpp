// Explicitly enabled serial protocols; existing installations default to legacy.
#pragma once
#include "hardware_config.hpp"
#include "uart_config.hpp"
#include <algorithm>
namespace forkrot {
struct ModbusConfig {
  uint8_t pc=0, bus=0, address=1, node=201, first=201, count=1;
  UartConfig uart0{}, uart1{115200,0,1};
  bool operator==(const ModbusConfig &b) const {
    return pc==b.pc && bus==b.bus && address==b.address && node==b.node && first==b.first && count==b.count && uart0==b.uart0 && uart1==b.uart1;
  }
  uint32_t transactionUs() const {return std::max<uint32_t>(80000u,uart1.wireUs(512)+20000u);}
  uint32_t tokenLeaseUs() const {return std::max<uint32_t>(300000u,transactionUs()*3);}
  uint32_t tokenRecoveryUs() const {return std::max<uint32_t>(1000000u,tokenLeaseUs()*3);}
  const char *validate() const {
    if(!uart0.valid()||!uart1.valid())return "invalid_uart_settings";
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
inline constexpr const char *SerialFieldNames[]={"pc","bus","address","node","first","count",
  "uart0Baud","uart0Parity","uart0StopBits","uart1Baud","uart1Parity","uart1StopBits"};
inline bool setSerialField(ModbusConfig &c,unsigned i,uint32_t value) {
  if(i>=12 || (i!=6&&i!=9&&value>255))return false;
  switch(i) {
    case 0:c.pc=value;break;case 1:c.bus=value;break;case 2:c.address=value;break;
    case 3:c.node=value;break;case 4:c.first=value;break;case 5:c.count=value;break;
    case 6:c.uart0.baud=value;break;case 7:c.uart0.parity=value;break;case 8:c.uart0.stopBits=value;break;
    case 9:c.uart1.baud=value;break;case 10:c.uart1.parity=value;break;case 11:c.uart1.stopBits=value;break;
  }
  return true;
}
using LegacyModbusBytes=std::array<uint8_t,16>;
using ModbusBytes=std::array<uint8_t,32>;
template<size_t N> inline void modbusChecksum(std::array<uint8_t,N> &b) {
  uint32_t crc=2166136261u;for(unsigned i=0;i<N-4;++i)crc=(crc^b[i])*16777619u;
  for(unsigned i=0;i<4;++i)b[N-4+i]=uint8_t(crc>>(8*i));
}
inline ModbusBytes encodeModbus(const ModbusConfig &c) {
  ModbusBytes b{'M','B',2,c.pc,c.bus,c.address,c.node,c.first,c.count};
  unsigned at=12;
  for(const auto &port:{c.uart0,c.uart1}) {
    for(unsigned i=0;i<4;++i)b[at++]=uint8_t(port.baud>>(8*i));
    b[at++]=port.parity;b[at++]=port.stopBits;
  }
  modbusChecksum(b);
  return b;
}
inline bool decodeModbus(const ModbusBytes &b,ModbusConfig &c) {
  ModbusConfig next{b[3],b[4],b[5],b[6],b[7],b[8]};
  unsigned at=12;
  for(auto *port:{&next.uart0,&next.uart1}) {
    port->baud=0;for(unsigned i=0;i<4;++i)port->baud|=uint32_t(b[at++])<<(8*i);
    port->parity=b[at++];port->stopBits=b[at++];
  }
  if(b[0]!='M'||b[1]!='B'||b[2]!=2||next.validate()||encodeModbus(next)!=b)return false;
  c=next;return true;
}
inline bool decodeModbus(const LegacyModbusBytes &b,ModbusConfig &c) {
  ModbusConfig next{b[3],b[4],b[5],b[6],b[7],b[8]};
  LegacyModbusBytes check{'M','B',1,next.pc,next.bus,next.address,next.node,next.first,next.count};
  modbusChecksum(check);
  if(next.validate()||check!=b)return false;
  next.uart0={next.pc?19200u:9600u,uint8_t(next.pc?1:0),1};
  next.uart1={115200,uint8_t(next.bus?1:0),1};
  c=next;return true;
}
struct ModbusStatus { uint32_t received=0,rejected=0,timeouts=0,passes=0,recoveries=0;uint8_t owner=0;bool fault=false; };
}
