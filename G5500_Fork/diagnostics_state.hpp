// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include <cstdint>
namespace forkrot {
inline constexpr int8_t RSSI_EMPTY = 127;
inline constexpr uint32_t RSSI_SAMPLE_MS = 10000;
struct RssiHistory {
  int8_t rssiHour[60], rssiDay[288], rssiWeek[336], rssiMonth[360], rssiYear[365];
  uint16_t rssiHourPos = 0, rssiHourCount = 0, rssiDayPos = 0, rssiDayCount = 0;
  uint16_t rssiWeekPos = 0, rssiWeekCount = 0, rssiMonthPos = 0, rssiMonthCount = 0,
           rssiYearPos = 0, rssiYearCount = 0;
  int32_t acc1m = 0, acc5m = 0, acc30m = 0, acc2h = 0, acc24h = 0;
  uint16_t cnt1m = 0, cnt5m = 0, cnt30m = 0, cnt2h = 0, cnt24h = 0;
  uint32_t rssiLastSampleMs = 0, rssiT1m = 0, rssiT5m = 0, rssiT30m = 0, rssiT2h = 0, rssiT24h = 0;
};
} // namespace forkrot
