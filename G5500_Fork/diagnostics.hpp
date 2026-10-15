// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include "board.hpp"
#include "storage.hpp"
#include "esp_system.h"
#include "byte_log.hpp"
namespace forkrot {
enum class RssiPeriod { Hour, Day, Week, Month, Year };
class Diagnostics {
public:
  explicit Diagnostics(Storage &storage) : storage_(storage) {}
  bool wifiCredentialsReadOk() const { return storage_.wifiCredentialsReadOk(); }
  void beginNativeUsb();
  void readBootReason();
  int bootReasonCode() const {
    return (int)bootResetReason;
  }
  const String &bootReasonText() const {
    return bootResetReasonText;
  }
  void addResetHistoryEntry() {
    storage_.addResetHistoryEntry((int)bootResetReason, bootResetReasonText);
  }
  String historyJson(RssiPeriod period);
  void diagClear();
  String diagSnapshot();
  void dbgPrint(const String &s);
  void dbgPrintln(const String &s);
  void dbgPstRx(const String &s);
  void dbgPstTx(const String &s);
  bool syncClockFromNtp(uint32_t timeoutMs = 3500);
  void initRssiHistory();
  void serviceRssiHistory();

private:
  void diagStoreLine(String line);
  void diagMirror(const String &s, bool newline);
  String resetReasonToText(esp_reset_reason_t r);
  void rssiPush(int8_t *buf, uint16_t size, uint16_t &pos, uint16_t &count, int8_t v);
  int8_t rssiAvg(int32_t sum, uint16_t cnt);
  String rssiHistoryJson(const int8_t *buf, uint16_t size, uint16_t pos, uint16_t count,
                         const char *range);
  Storage &storage_;
  RssiHistory rssi_;
  esp_reset_reason_t bootResetReason = ESP_RST_UNKNOWN;
  String bootResetReasonText = "UNKNOWN";
#if SELECTED_BOARD == BOARD_ESP32
  ByteLog<2048> diagLog_;
#else
  static constexpr uint16_t DIAG_MAX_LINES = 160;
  static constexpr uint16_t DIAG_MAX_LINE_LEN = 220;
  String diagLines[DIAG_MAX_LINES];
  uint16_t diagHead = 0;
  uint16_t diagCount = 0;
  String diagPending;
#endif
};
} // namespace forkrot
