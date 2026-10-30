#include "wifi_access.hpp"
// G5500 Fork; original by UA1CFM. GPL-3.0.
#include "diagnostics.hpp"
#include "ethernet.hpp"
#include <time.h>

#if ARDUINO_USB_MODE
#include <HWCDC.h>
#if !ARDUINO_USB_CDC_ON_BOOT
static HWCDC HWCDCSerial;
#endif
#elif ARDUINO_USB_CDC_ON_BOOT
#include <USB.h>
static auto &HWCDCSerial = Serial;
#endif

namespace forkrot {
void Diagnostics::diagMirror(const String &s, bool newline) {
#if LOG_HISTORY_SUPPORT
  diagLog_.append({s.c_str(), s.length()}, newline);
#else
  (void)s; (void)newline;
#endif
}
void Diagnostics::diagClear() {
#if LOG_HISTORY_SUPPORT
  diagLog_.clear();
#endif
}
void Diagnostics::dbgView(std::string_view text, bool newline) {
#if LOG_HISTORY_SUPPORT
  diagLog_.append(text, newline);
#if FORK_NATIVE_USB && !CROWPANEL_USB_TRACE && !FORK_CLASSIC_ESP32 && !FORK_C3
  if (!nativeCommandMode_ && HWCDCSerial) {
    HWCDCSerial.write(reinterpret_cast<const uint8_t *>(text.data()), text.size());
    if (newline) HWCDCSerial.println();
  }
#endif
#else
  (void)text; (void)newline;
#endif
}
void Diagnostics::dbgPrint(const String &s) {
  diagMirror(s, false);
#if FORK_NATIVE_USB && !CROWPANEL_USB_TRACE
  if (!nativeCommandMode_ && HWCDCSerial) {
    HWCDCSerial.print(s);
  }
#endif
}

void Diagnostics::dbgPrintln(const String &s) {
  diagMirror(s, true);
#if FORK_NATIVE_USB && !CROWPANEL_USB_TRACE
  if (!nativeCommandMode_ && HWCDCSerial) {
    HWCDCSerial.println(s);
  }
#endif
}

void Diagnostics::dbgPstTx(const String &s) {
  dbgPrint("[PST TX] ");
  String t = s;
  t.replace("\r", "");
  t.replace("\n", "");
  dbgPrintln(t);
}

String Diagnostics::resetReasonToText(esp_reset_reason_t r) {
  switch (r) {
  case ESP_RST_POWERON:
    return "POWERON";
  case ESP_RST_EXT:
    return "EXTERNAL";
  case ESP_RST_SW:
    return "SOFTWARE";
  case ESP_RST_PANIC:
    return "PANIC";
  case ESP_RST_INT_WDT:
    return "INT_WDT";
  case ESP_RST_TASK_WDT:
    return "TASK_WDT";
  case ESP_RST_WDT:
    return "WDT";
  case ESP_RST_DEEPSLEEP:
    return "DEEPSLEEP";
  case ESP_RST_BROWNOUT:
    return "BROWNOUT";
  case ESP_RST_SDIO:
    return "SDIO";
  case ESP_RST_USB:
    return "USB";
  default:
    return "UNKNOWN";
  }
}

bool Diagnostics::syncClockFromNtp(uint32_t timeoutMs) {
#if NTP_SUPPORT

  if (!ethernetConnected() && radioWifi.status() != WL_CONNECTED) {
    return false;
  }
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  uint32_t t0 = millis();
  time_t now = 0;
  while (millis() - t0 < timeoutMs) {
    now = time(nullptr);
    if (now > 1700000000) {
      return true; // sane Unix time
    }
    delay(100);
  }
  return false;

#else
(void)timeoutMs; return false;
#endif
}

void Diagnostics::initRssiHistory() {
#if RSSI_HISTORY_SUPPORT

  memset(rssi_.rssiHour, RSSI_EMPTY, sizeof(rssi_.rssiHour));
  memset(rssi_.rssiDay, RSSI_EMPTY, sizeof(rssi_.rssiDay));
  memset(rssi_.rssiWeek, RSSI_EMPTY, sizeof(rssi_.rssiWeek));
  memset(rssi_.rssiMonth, RSSI_EMPTY, sizeof(rssi_.rssiMonth));
  memset(rssi_.rssiYear, RSSI_EMPTY, sizeof(rssi_.rssiYear));
  storage_.loadRssiYear(rssi_);
  if (rssi_.rssiYearPos >= 365) {
    rssi_.rssiYearPos = 0;
  }
  if (rssi_.rssiYearCount > 365) {
    rssi_.rssiYearCount = 365;
  }
  uint32_t now = millis();
  rssi_.rssiT1m = rssi_.rssiT5m = rssi_.rssiT30m = rssi_.rssiT2h = rssi_.rssiT24h = now;

#else

#endif
}

void Diagnostics::rssiPush(int8_t *buf, uint16_t size, uint16_t &pos, uint16_t &count, int8_t v) {
  buf[pos] = v;
  pos = (uint16_t)((pos + 1) % size);
  if (count < size) {
    count++;
  }
}

int8_t Diagnostics::rssiAvg(int32_t sum, uint16_t cnt) {
  if (!cnt) {
    return RSSI_EMPTY;
  }
  int v = (int)lroundf((float)sum / (float)cnt);
  if (v < -127) {
    v = -127;
  }
  if (v > 0) {
    v = 0;
  }
  return (int8_t)v;
}

void Diagnostics::serviceRssiHistory() {
#if RSSI_HISTORY_SUPPORT

  uint32_t now = millis();
  if (now - rssi_.rssiLastSampleMs < RSSI_SAMPLE_MS) {
    return;
  }
  rssi_.rssiLastSampleMs = now;
  if (radioWifi.status() == WL_CONNECTED) {
    int r = radioWifi.RSSI();
    if (r < -127) {
      r = -127;
    }
    if (r > 0) {
      r = 0;
    }
    rssi_.acc1m += r;
    rssi_.acc5m += r;
    rssi_.acc30m += r;
    rssi_.acc2h += r;
    rssi_.acc24h += r;
    rssi_.cnt1m++;
    rssi_.cnt5m++;
    rssi_.cnt30m++;
    rssi_.cnt2h++;
    rssi_.cnt24h++;
  }
  if (now - rssi_.rssiT1m >= 60000UL) {
    rssiPush(rssi_.rssiHour, 60, rssi_.rssiHourPos, rssi_.rssiHourCount,
             rssiAvg(rssi_.acc1m, rssi_.cnt1m));
    rssi_.acc1m = 0;
    rssi_.cnt1m = 0;
    rssi_.rssiT1m = now;
  }
  if (now - rssi_.rssiT5m >= 300000UL) {
    rssiPush(rssi_.rssiDay, 288, rssi_.rssiDayPos, rssi_.rssiDayCount,
             rssiAvg(rssi_.acc5m, rssi_.cnt5m));
    rssi_.acc5m = 0;
    rssi_.cnt5m = 0;
    rssi_.rssiT5m = now;
  }
  if (now - rssi_.rssiT30m >= 1800000UL) {
    rssiPush(rssi_.rssiWeek, 336, rssi_.rssiWeekPos, rssi_.rssiWeekCount,
             rssiAvg(rssi_.acc30m, rssi_.cnt30m));
    rssi_.acc30m = 0;
    rssi_.cnt30m = 0;
    rssi_.rssiT30m = now;
  }
  if (now - rssi_.rssiT2h >= 7200000UL) {
    rssiPush(rssi_.rssiMonth, 360, rssi_.rssiMonthPos, rssi_.rssiMonthCount,
             rssiAvg(rssi_.acc2h, rssi_.cnt2h));
    rssi_.acc2h = 0;
    rssi_.cnt2h = 0;
    rssi_.rssiT2h = now;
  }
  if (now - rssi_.rssiT24h >= 86400000UL) {
    rssiPush(rssi_.rssiYear, 365, rssi_.rssiYearPos, rssi_.rssiYearCount,
             rssiAvg(rssi_.acc24h, rssi_.cnt24h));
    rssi_.acc24h = 0;
    rssi_.cnt24h = 0;
    rssi_.rssiT24h = now;
    storage_.persistRssiYear(rssi_);
  }

#else

#endif
}

bool Diagnostics::rssiHistoryJson(JsonWriter &j, const int8_t *buf, uint16_t size, uint16_t pos, uint16_t count,
                                    const char *range) {
  j << "{\"range\":" << jsonQuoted(range) << ",\"values\":[";
  int minv = 0, maxv = -127;
  long sum = 0;
  uint16_t valid = 0;
  count = std::min(count, size);
  uint16_t start = (count < size) ? 0 : pos;
  for (uint16_t i = 0; i < count; i++) {
    int v = buf[(start + i) % size];
    if (i) j << ',';
    if (v == RSSI_EMPTY) j << "null";
    else {
      j << v;
      if (!valid || v < minv) minv = v;
      if (!valid || v > maxv) maxv = v;
      sum += v; ++valid;
    }
  }
  j << "],\"count\":" << count;
  if (valid) {
    j << ",\"min\":" << minv << ",\"max\":" << maxv << ",\"avg\":";
    j.decimal(double(sum) / valid, 1);
  } else j << ",\"min\":null,\"max\":null,\"avg\":null";
  j << '}';
  return j.good();
}
void Diagnostics::beginNativeUsb() {
#if FORK_NATIVE_USB
  HWCDCSerial.begin();
  // A USB host can stay connected after flashing without consuming output.
  // Never let its full TX buffer delay setup, Wi-Fi retries or the main loop.
  // The complete diagnostic log is retained in RAM even after a short write.
  HWCDCSerial.setTxTimeoutMs(0);
#endif
}
void Diagnostics::readBootReason() {
  bootResetReason = esp_reset_reason();
  bootResetReasonText = resetReasonToText(bootResetReason);
}
bool Diagnostics::writeHistoryJson(JsonWriter &j, RssiPeriod period) {
#if RSSI_HISTORY_SUPPORT

  if (period == RssiPeriod::Hour) {
    return rssiHistoryJson(j, rssi_.rssiHour, 60, rssi_.rssiHourPos, rssi_.rssiHourCount, "hour");
  }
  if (period == RssiPeriod::Day) {
    return rssiHistoryJson(j, rssi_.rssiDay, 288, rssi_.rssiDayPos, rssi_.rssiDayCount, "day");
  }
  if (period == RssiPeriod::Week) {
    return rssiHistoryJson(j, rssi_.rssiWeek, 336, rssi_.rssiWeekPos, rssi_.rssiWeekCount, "week");
  }
  if (period == RssiPeriod::Month) {
    return rssiHistoryJson(j, rssi_.rssiMonth, 360, rssi_.rssiMonthPos, rssi_.rssiMonthCount, "month");
  }
  return rssiHistoryJson(j, rssi_.rssiYear, 365, rssi_.rssiYearPos, rssi_.rssiYearCount, "year");

#else
j << "{}"; (void)period; return false;
#endif
}

} // namespace forkrot

namespace forkrot {
void Diagnostics::writeLog(JsonWriter &out) const {
#if LOG_HISTORY_SUPPORT
  struct Sink { JsonWriter &writer; void operator+=(char c) { writer << c; } } sink{out};
  diagLog_.copyTo(sink);
#else
  (void)out;
#endif
}
}

namespace forkrot {
int Diagnostics::nativeAvailable() {
#if FORK_NATIVE_USB && !CROWPANEL_USB_TRACE
  return HWCDCSerial.available();
#else
  return 0;
#endif
}
int Diagnostics::nativeRead() {
#if FORK_NATIVE_USB && !CROWPANEL_USB_TRACE
  return HWCDCSerial.read();
#else
  return -1;
#endif
}
size_t Diagnostics::nativeWrite(const uint8_t *data, size_t size) {
#if FORK_NATIVE_USB && !CROWPANEL_USB_TRACE
  const int available=HWCDCSerial.availableForWrite();
  return available>0?HWCDCSerial.write(data,std::min(size,size_t(available))):0;
#else
  (void)data;(void)size;return 0;
#endif
}
}

#if CROWPANEL_USB_TRACE
namespace forkrot {
bool Diagnostics::traceUsbConnected() { return bool(HWCDCSerial); }
size_t Diagnostics::traceUsbWrite(const uint8_t *data, size_t size) {
  if (!HWCDCSerial) return 0;
  const int available = HWCDCSerial.availableForWrite();
  return available > 0 ? HWCDCSerial.write(data, std::min(size, size_t(available))) : 0;
}
}
#endif
