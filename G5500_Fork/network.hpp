// G5500 Fork; original by UA1CFM, modifications by RA3VLD. GPL-3.0.
#pragma once
#include "runtime.hpp"
#include "board.hpp"
#include "network_state.hpp"
#include "device_actions.hpp"
#include "hardware.hpp"
#include "diagnostics.hpp"
#include "input.hpp"
#include <WiFi.h>
#include <WiFiUdp.h>
namespace forkrot {
class Network {
public:
  Network(Runtime &runtime, DeviceActions &actions, Calibration &calibration, Hardware &hardware,
          Diagnostics &diagnostics)
      : runtime_(runtime), actions_(actions), calibration_(calibration), hardware_(hardware),
        diagnostics_(diagnostics) {}
  void beginSerial();
  WifiCredentials &credentials() {
    return credentials_;
  }
  const WifiCredentials &credentials() const {
    return credentials_;
  }
  bool tcpConnected() {
    return rotctldClient_ && rotctldClient_.connected();
  }
  void handlePstRotatorSerial();
  bool hc05Connected();
  void handleHc05Serial();
  void handleRotctldTcp();
  void startWifi();
  void serviceWifiReconnect();
  void setupLoggerUdp();
  void serviceLoggerUdp();

private:
  void sendReply(const String &s);
  void processGs232Command(String cmd);
  void rotctldSend(const String &s);
  void processRotctldCommand(String cmd);
  void restoreApOnlyAfterReconnectFail();
  Runtime &runtime_;
  DeviceActions &actions_;
  Calibration &calibration_;
  Hardware &hardware_;
  Diagnostics &diagnostics_;

  WifiCredentials credentials_;
  WiFiServer tcpServer_{board::TCP_PORT};
  WiFiClient rotctldClient_;
#if BLUETOOTH_ENABLED
  HardwareSerial hc05Serial_{1};
#endif
  WiFiUDP n1mmUdp, pstUdp;
  enum GsReplyPort : uint8_t { GS_REPLY_USB = 0, GS_REPLY_HC05 = 1 };
  GsReplyPort gsReplyPort = GS_REPLY_USB;
  static constexpr uint32_t WIFI_RECONNECT_MS = 30000, WIFI_RECONNECT_TIMEOUT_MS = 8000;
  uint32_t wifiReconnectMs = 0, wifiReconnectTryStartedMs = 0;
  bool wifiReconnectTryActive = false;
  struct ReportPeer {
    IPAddress address;
    bool used = false;
  };
  ReportPeer reportPeers[8];
  uint8_t nextReportPeer = 0;
  uint32_t lastReportMs = 0;
};
} // namespace forkrot
