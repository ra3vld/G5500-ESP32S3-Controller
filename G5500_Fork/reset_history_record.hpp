// G5500 Fork; GPL-3.0.
#pragma once
#include <cstdint>
#include <cstring>
namespace forkrot {
// ESP-IDF reset codes are stable. Unrecognized/custom text keeps the legacy
// representation instead of losing information during compaction.
inline const char *resetHistoryReasonName(unsigned code) {
  constexpr const char *names[]={"UNKNOWN","POWERON","EXTERNAL","SOFTWARE","PANIC","INT_WDT",
    "TASK_WDT","WDT","DEEPSLEEP","BROWNOUT","SDIO","USB"};
  return code<sizeof names/sizeof *names?names[code]:"UNKNOWN";
}
inline constexpr uint64_t ResetHistoryTag=uint64_t(0x5253)<<48;
inline constexpr uint64_t ResetHistoryEpochMask=(uint64_t(1)<<40)-1;
inline bool packResetHistory(uint64_t epoch,int code,const char *reason,uint64_t &value) {
  if(epoch>ResetHistoryEpochMask || code<0 || code>255 || std::strcmp(reason,resetHistoryReasonName(unsigned(code))))return false;
  value=ResetHistoryTag|(epoch<<8)|unsigned(code);return true;
}
inline bool unpackResetHistory(uint64_t value,uint64_t &epoch,int &code) {
  if((value&(~uint64_t(0)<<48))!=ResetHistoryTag)return false;
  epoch=(value>>8)&ResetHistoryEpochMask;code=int(value&255);return true;
}
}
