// G5500 Fork; GPL-3.0.
#pragma once
#include <cstddef>
#include <cstdint>
namespace forkrot {
// POD only: safe in RTC_NOINIT, with no startup constructor or heap ownership.
struct AllocationRecord {
  uint32_t magic,version,size,caps,freeBytes,largestBlock;
  uint32_t pc[8];
  char task[16],function[32];
  uint8_t elf[32];
  uint32_t checksum;
};
inline constexpr uint32_t AllocationRecordMagic=0x4f4f4d31;
inline uint32_t allocationRecordChecksum(const AllocationRecord &r) {
  const auto *p=reinterpret_cast<const uint8_t *>(&r);
  uint32_t hash=2166136261u;
  for(size_t i=sizeof r.magic;i<offsetof(AllocationRecord,checksum);++i)hash=(hash^p[i])*16777619u;
  return hash;
}
inline bool allocationRecordValid(const AllocationRecord &r) {
  return __atomic_load_n(&r.magic,__ATOMIC_ACQUIRE)==AllocationRecordMagic &&
      r.version==1 && r.task[15]==0 && r.function[31]==0 && r.checksum==allocationRecordChecksum(r);
}
inline void allocationRecordText(char *out,size_t capacity,const char *in) {
  size_t i=0;for(;in&&i+1<capacity&&in[i];++i)out[i]=in[i];
  for(;i<capacity;++i)out[i]=0;
}
template<class Failure> inline void captureAllocationRecord(AllocationRecord &r,const Failure &f,const uint8_t *elf) {
  uint32_t empty=0;
  if(!__atomic_compare_exchange_n(&r.magic,&empty,1,false,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED))return;
  r.version=1;r.size=f.size;r.caps=f.caps;r.freeBytes=f.freeBytes;r.largestBlock=f.largestBlock;
  for(unsigned i=0;i<8;++i)r.pc[i]=f.pc[i];
  allocationRecordText(r.task,sizeof r.task,f.task);
  allocationRecordText(r.function,sizeof r.function,f.function);
  for(unsigned i=0;i<32;++i)r.elf[i]=elf?elf[i]:0;
  r.checksum=allocationRecordChecksum(r);
  __atomic_store_n(&r.magic,AllocationRecordMagic,__ATOMIC_RELEASE);
}
} // namespace forkrot
