// G5500 Fork; original by UA1CFM. GPL-3.0.
#include "diagnostics.hpp"
#include <WiFi.h>
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
void Diagnostics::diagStoreLine(String line) {
#if SELECTED_BOARD == BOARD_ESP32
  diagLog_.append({line.c_str(), line.length()}, true);
#else
  line.replace("\r", "");
  if (line.length() > DIAG_MAX_LINE_LEN) {
    line = line.substring(0, DIAG_MAX_LINE_LEN - 3) + "...";
  }
  diagLines[diagHead] = line;
  diagHead = (diagHead + 1) % DIAG_MAX_LINES;
  if (diagCount < DIAG_MAX_LINES) {
    diagCount++;
  }
#endif
}

void Diagnostics::diagMirror(const String &s, bool newline) {
#if SELECTED_BOARD == BOARD_ESP32
  diagLog_.append({s.c_str(), s.length()}, newline);
#else
  for (size_t i = 0; i < s.length(); i++) {
    char ch = s[i];
    if (ch == '\r') {
      continue;
    }
    if (ch == '\n') {
      diagStoreLine(diagPending);
      diagPending = "";
    } else {
      diagPending += ch;
      if (diagPending.length() >= DIAG_MAX_LINE_LEN) {
        diagStoreLine(diagPending);
        diagPending = "";
      }
    }
  }
  if (newline) {
    diagStoreLine(diagPending);
    diagPending = "";
  }
#endif
}

void Diagnostics::diagClear() {
#if SELECTED_BOARD == BOARD_ESP32
  diagLog_.clear();
#else
  for (uint16_t i = 0; i < DIAG_MAX_LINES; i++) {
    diagLines[i] = "";
  }
  diagHead = 0;
  diagCount = 0;
  diagPending = "";
#endif
}

String Diagnostics::diagSnapshot() {
  String out;
#if SELECTED_BOARD == BOARD_ESP32
  if (!out.reserve(diagLog_.size())) return "[DIAG] not enough memory for snapshot\n";
  diagLog_.copyTo(out);
#else
  out.reserve(8192);
  uint16_t start = (diagHead + DIAG_MAX_LINES - diagCount) % DIAG_MAX_LINES;
  for (uint16_t i = 0; i < diagCount; i++) {
    uint16_t idx = (start + i) % DIAG_MAX_LINES;
    out += diagLines[idx];
    out += '\n';
  }
  if (diagPending.length()) {
    out += diagPending;
  }
#endif
  return out;
}

void Diagnostics::dbgPrint(const String &s) {
  diagMirror(s, false);
#if FORK_NATIVE_USB
  if (HWCDCSerial) {
    HWCDCSerial.print(s);
  }
#endif
}

void Diagnostics::dbgPrintln(const String &s) {
  diagMirror(s, true);
#if FORK_NATIVE_USB
  if (HWCDCSerial) {
    HWCDCSerial.println(s);
  }
#endif
}

void Diagnostics::dbgPstRx(const String &s) {
  dbgPrint("[PST RX] ");
  dbgPrintln(s);
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
  default:
    return "UNKNOWN";
  }
}

bool Diagnostics::syncClockFromNtp(uint32_t timeoutMs) {
  if (WiFi.status() != WL_CONNECTED) {
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
}

void Diagnostics::initRssiHistory() {
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
  uint32_t now = millis();
  if (now - rssi_.rssiLastSampleMs < RSSI_SAMPLE_MS) {
    return;
  }
  rssi_.rssiLastSampleMs = now;
  if (WiFi.status() == WL_CONNECTED) {
    int r = WiFi.RSSI();
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
}

String Diagnostics::rssiHistoryJson(const int8_t *buf, uint16_t size, uint16_t pos, uint16_t count,
                                    const char *range) {
  String j;
  j.reserve(2600);
  j = "{\"range\":\"";
  j += range;
  j += "\",\"values\":[";
  int minv = 0, maxv = -127;
  long sum = 0;
  uint16_t valid = 0;
  uint16_t start = (count < size) ? 0 : pos;
  for (uint16_t i = 0; i < count; i++) {
    uint16_t idx = (uint16_t)((start + i) % size);
    int8_t v = buf[idx];
    if (i) {
      j += ',';
    }
    if (v == RSSI_EMPTY) {
      j += "null";
    } else {
      j += String((int)v);
      int iv = (int)v;
      if (!valid || iv < minv) {
        minv = iv;
      }
      if (!valid || iv > maxv) {
        maxv = iv;
      }
      sum += iv;
      valid++;
    }
  }
  j += "],\"count\":" + String(count);
  if (valid) {
    j += ",\"min\":" + String(minv) + ",\"max\":" + String(maxv) +
         ",\"avg\":" + String((float)sum / valid, 1);
  } else {
    j += ",\"min\":null,\"max\":null,\"avg\":null";
  }
  j += "}";
  return j;
}
void Diagnostics::beginNativeUsb() {
#if FORK_NATIVE_USB
  HWCDCSerial.begin();
#endif
}
void Diagnostics::readBootReason() {
  bootResetReason = esp_reset_reason();
  bootResetReasonText = resetReasonToText(bootResetReason);
}
String Diagnostics::historyJson(RssiPeriod period) {
  if (period == RssiPeriod::Hour) {
    return rssiHistoryJson(rssi_.rssiHour, 60, rssi_.rssiHourPos, rssi_.rssiHourCount, "hour");
  }
  if (period == RssiPeriod::Day) {
    return rssiHistoryJson(rssi_.rssiDay, 288, rssi_.rssiDayPos, rssi_.rssiDayCount, "day");
  }
  if (period == RssiPeriod::Week) {
    return rssiHistoryJson(rssi_.rssiWeek, 336, rssi_.rssiWeekPos, rssi_.rssiWeekCount, "week");
  }
  if (period == RssiPeriod::Month) {
    return rssiHistoryJson(rssi_.rssiMonth, 360, rssi_.rssiMonthPos, rssi_.rssiMonthCount, "month");
  }
  return rssiHistoryJson(rssi_.rssiYear, 365, rssi_.rssiYearPos, rssi_.rssiYearCount, "year");
}

} // namespace forkrot
