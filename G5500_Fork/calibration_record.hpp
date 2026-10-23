#pragma once
#include "calibration.hpp"
#include <array>
#include <cstring>
namespace forkrot {
// Fixed little-endian words; no persisted C++ layout, pointers or padding.
using CalibrationRecord = std::array<uint8_t, 512>;
using CalibrationRecordV1 = std::array<uint8_t, 384>;
inline uint32_t calibrationCrc(const uint8_t *data, size_t size) {
  uint32_t crc = ~0u;
  for (size_t i=0; i<size; ++i) {
    crc ^= data[i];
    for (int b=0; b<8; ++b) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
  }
  return ~crc;
}
template<size_t N = 512> inline std::array<uint8_t,N> calibrationRecord(const CalibrationState &s) {
  static_assert(N == 384 || N == 512);
  std::array<uint8_t,N> out{};
  size_t at=0;
  const auto putWord=[&](uint32_t v) { for (int b=0;b<4;++b) out[at++]=uint8_t(v >> (8*b)); };
  const auto real=[&](float v) { uint32_t w; std::memcpy(&w,&v,4); putWord(w); };
  putWord(0x31525a41); putWord(N == 384 ? 1 : 3); putWord(s.gsAzMode); putWord(s.calibrationMode);
  for (int v : {s.cal.az36_0,s.cal.az36Max,s.cal.az45_0,s.cal.az45Max,s.cal.el0,s.cal.elMax}) putWord(uint32_t(v));
  for (bool v : {s.az36OSet,s.az36FSet,s.az45OSet,s.az45FSet,s.elOSet,s.elFSet}) putWord(v);
  for (int v : s.az36CalMv) putWord(uint32_t(v));
  for (int v : s.az45CalMv) putWord(uint32_t(v));
  for (int v : s.elCalMv) putWord(uint32_t(v));
  const auto custom=[&](const CustomAxisCalibration &c) {
  putWord(c.initialized); real(c.range.start); real(c.range.end);
  putWord(uint32_t(c.zeroMv)); putWord(uint32_t(c.fullMv)); putWord(c.zeroSet); putWord(c.fullSet); putWord(c.count);
  for (unsigned i=0;i<AZ_CUSTOM_CAL_N;++i) { real(c.degrees[i]); putWord(uint32_t(c.millivolts[i])); }
  };
  custom(s.custom);
  if constexpr (N == 512) { putWord(s.elProfile); custom(s.customEl); }
  if constexpr (N == 512) {
    at=480;
    for(auto id:s.sensorIdentity) {putWord(uint32_t(id));putWord(uint32_t(id>>32));}
  }
  at=out.size()-4; putWord(calibrationCrc(out.data(),out.size()-4));
  return out;
}
template<size_t N> inline bool readCalibrationRecord(const std::array<uint8_t,N> &in, CalibrationState &s) {
  static_assert(N == 384 || N == 512);
  size_t at=0; bool ok=true;
  const auto getWord=[&]() { uint32_t v=0; for (int b=0;b<4;++b) v|=uint32_t(in[at++])<<(8*b); return v; };
  const auto real=[&]() { uint32_t w=getWord(); float v; std::memcpy(&v,&w,4); ok &= std::isfinite(v); return v; };
  const auto boolean=[&]() { auto v=getWord(); ok &= v<=1; return v!=0; };
  const auto mv=[&]() { int32_t v=int32_t(getWord()); ok &= v>=-1 && v<=16383; return int(v); };
  at=in.size()-4;
  if (getWord()!=calibrationCrc(in.data(),in.size()-4)) return false;
  at=0;
  if (getWord()!=0x31525a41) return false;
  const auto version=getWord();
  if(N==384 ? version!=1 : (version!=2 && version!=3)) return false;
  auto profile=getWord(), mode=getWord();
  if ((profile!=0 && profile!=36 && profile!=45) || mode>1) return false;
  CalibrationState next;
  next.gsAzMode=uint8_t(profile); next.calibrationMode=CalibrationMode(mode);
  for (int *v : {&next.cal.az36_0,&next.cal.az36Max,&next.cal.az45_0,&next.cal.az45Max,&next.cal.el0,&next.cal.elMax}) *v=mv();
  for (bool *v : {&next.az36OSet,&next.az36FSet,&next.az45OSet,&next.az45FSet,&next.elOSet,&next.elFSet}) *v=boolean();
  for (int &v : next.az36CalMv) v=mv();
  for (int &v : next.az45CalMv) v=mv();
  for (int &v : next.elCalMv) v=mv();
  const auto custom=[&](CustomAxisCalibration &c, bool selected) {
  c.initialized=boolean(); c.range.start=real(); c.range.end=real();
  c.zeroMv=mv(); c.fullMv=mv(); c.zeroSet=boolean(); c.fullSet=boolean();
  auto count=getWord(); if (count<2 || count>AZ_CUSTOM_CAL_N) return false; c.count=uint8_t(count);
  for (unsigned i=0;i<AZ_CUSTOM_CAL_N;++i) { c.degrees[i]=real(); c.millivolts[i]=mv(); }
  if (!ok || !c.range.valid() || (selected && !c.initialized)) return false;
  if (c.degrees[0]!=0 || std::abs(c.degrees[c.count-1]-c.range.span())>.001f) return false;
  for (unsigned i=1;i<c.count;++i) if (c.degrees[i]<=c.degrees[i-1] || c.degrees[i]>c.range.span()+.001f) return false;
  return true;
  };
  if (!custom(next.custom,profile==AZ_CUSTOM)) return false;
  if constexpr (N == 512) {
    auto el=getWord();if(el!=EL_CUSTOM && el!=EL_STANDARD)return false;
    next.elProfile=uint8_t(el);
    if(!custom(next.customEl,el==EL_CUSTOM))return false;
    if(version==3) {at=480;for(auto &id:next.sensorIdentity){id=getWord();id|=uint64_t(getWord())<<32;}}
  }
  next.az36LinearValid=next.az36OSet && next.az36FSet && std::abs(next.cal.az36Max-next.cal.az36_0)>100;
  next.az45LinearValid=next.az45OSet && next.az45FSet && std::abs(next.cal.az45Max-next.cal.az45_0)>100;
  next.elLinearValid=next.elOSet && next.elFSet && std::abs(next.cal.elMax-next.cal.el0)>100;
  s=next;
  return true;
}
}
