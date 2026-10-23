// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include "http_diagnostics.hpp"
#include "runtime.hpp"
#include "board.hpp"
#include "network_state.hpp"
#include "ethernet.hpp"
#include "device_actions.hpp"
#include "hardware.hpp"
#include "diagnostics.hpp"
#include "input.hpp"
#include "bluetooth.hpp"
#include "heap_metrics.hpp"
#include "datagram_socket.hpp"
#include "wifi_fallback.hpp"
#include "diagnostic_stream.hpp"
#include "command_api.hpp"
#include <WiFi.h>
#include <WiFiClient.h>
#include "hud_uart_tx.hpp"
#include "rs485_hud.hpp"
#include "rs485_reply.hpp"
#include "modbus_serial.hpp"
#include "modbus_bus.hpp"
#include <WiFiServer.h>
namespace forkrot {
class Network : public HudTransport {
public:
  Network(Runtime &runtime, DeviceActions &actions, Calibration &calibration, Hardware &hardware,
          Diagnostics &diagnostics)
      : runtime_(runtime), actions_(actions), calibration_(calibration), hardware_(hardware),
        diagnostics_(diagnostics) {}
  bool beginSerial();
  void beginHud();
  void serviceHud();
  bool sendHud(const HudEndpoint &, const uint8_t *, size_t) override;
  void selectionChanged() override { resetHudInputs(); }
  bool hudPortValid(unsigned port, unsigned n1mm, unsigned pst, unsigned report) const;
  bool apAlwaysOn() const {
    return wifiSetupAp_ || runtime_.recovery || runtime_.hud.config.wifi == HudWifi::Host ||
           (runtime_.hud.config.wifi == HudWifi::Existing && (!FORK_CLASSIC_ESP32 || runtime_.controller.hardware.forceAp));
  }

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
  bool bluetoothConnected() { return bluetooth_.connected(); }
  bool bluetoothLinkConnected() { return bluetooth_.linkConnected(); }
  bool bluetoothMac(char (&address)[18]) const { return bluetooth_.macAddress(address); }
  void handleBluetoothSerial();
  void serviceHeapDiagnostics();
  bool beginCommandApi();
  void setupCommandUdp();
  void serviceCommandUdp();
  void serviceCommandReplies();
  bool portsValid(unsigned hud, unsigned n1mm, unsigned pst, unsigned report, unsigned command) const {
    return commandPortAllowed(command) && command!=hud && command!=n1mm && command!=pst &&
        command!=pst+1 && command!=report && hud && hud<=65535 && hud!=n1mm &&
        hud!=pst && hud!=pst+1 && hud!=report;
  }
  void handleRotctldTcp();
  bool startWifi();
  void connectWifi(const String &ssid, const String &password);
  bool ethernetStarted() const { return ethernet_.started(); }
  bool wifiConnecting() const { return wifiConnectPending_ || (wifiFallback_.trying() && WiFi.status() != WL_CONNECTED); }
  void formatAddresses(char *text, size_t capacity, const char *separator = "\n") const;
  void serviceWifiReconnect();
  void setupLoggerUdp();
  void serviceLoggerUdp();
  void captureSetupHeap() { heapAfterSetup_ = heapFreeBytes(); }
  void setHttpDiagnostics(const HttpDiagnostics &state) { http_ = state; }
  const HttpDiagnostics &httpDiagnostics() const { return http_; }

private:
  struct ModbusLink {
    mb::SerialPort pc,bus;
    mb::Bus link;
    mb::Frame pcResponse;
    bool busReady=false;
    uint32_t pcReceived=0,pcRejected=0,pcTimeouts=0;
    explicit ModbusLink(Runtime &r):link(r.controller,r.hud){}
  };
  std::unique_ptr<ModbusLink> modbus_;
  void serviceModbusPc();
  void serviceModbusBus();
  size_t uartWrite(const uint8_t *,size_t);
  void serviceUartTx();
  std::unique_ptr<Rs485Reply> rs485Reply_;
  void sendReply(const String &s);
  void processGs232Command(std::string_view input);
  bool processCommandApi(std::string_view input, uint8_t port);
  size_t runCommandApi(uint64_t peer, std::string_view input, char *reply, size_t capacity);
  void rotctldSend(const String &s);
  void processRotctldCommand(std::string_view input);
  void restoreApOnlyAfterReconnectFail();
  void serviceApSubnet();
  int formatHeapDiagnostics(uint8_t page, char *reply, size_t capacity);
  Runtime &runtime_;
  DeviceActions &actions_;
  Calibration &calibration_;
  Hardware &hardware_;
  Diagnostics &diagnostics_;

  DatagramSocket hudUdp_;
  uint8_t hudRx_[HUD_FRAME_MAX]{};
  struct UartLink {
    HudUartDecoder decoder;
    HudUartTx tx;
  };
  std::unique_ptr<UartLink> hudUart_;
  struct Rs485Link { HardwareSerial serial{1}; Rs485Hud link; };
  std::unique_ptr<Rs485Link> hud485_;
  uint32_t inputEpoch_ = 0, tcpSession_ = 0, bluetoothSession_ = 0;
  struct DeferredReply {
    uint32_t id = 0, session = 0;
    uint8_t port = 0;
    bool ham = false, used = false;
  };
  DeferredReply deferred_[4];
  bool deferReply(const Packet &, uint8_t port);
  void resetHudInputs();
  WifiCredentials credentials_;
  EthernetAdapter ethernet_;
  bool ethernetWasConnected_=false;
  WiFiServer tcpServer_{board::TCP_PORT};
  WiFiClient rotctldClient_;
  SelectionApiLineBuffer tcpReceiver_;
  std::unique_ptr<CommandServer> commandApi_;
  DatagramSocket commandUdp_;
  char commandReply_[COMMAND_REPLY_MAX + 5]{};
  size_t commandReplySize_ = 0, commandReplySent_ = 0;
  uint32_t commandReplyAt_ = 0, commandReplySession_ = 0;
  uint8_t commandReplyPort_ = 0;
  uint8_t commandInterruptedPorts_ = 0;
  bool commandRebind_ = false;
  uint32_t commandRebindAt_ = 0;
  Bluetooth bluetooth_;
  uint32_t lastBtDropped_ = 0;
  bool btReady_ = false;
  bool compactWifi_ = false;
  bool bufferedWifi_ = false;
  DatagramSocket n1mmUdp, pstUdp;
  enum GsReplyPort : uint8_t { GS_REPLY_USB = 0, GS_REPLY_BLUETOOTH = 1, GS_REPLY_NATIVE_USB = 2 };
  GsReplyPort gsReplyPort = GS_REPLY_USB;
  WifiFallback wifiFallback_;
  bool wifiSetupAp_ = false, wifiConnectPending_ = false;
  uint32_t wifiConnectRequestedMs_ = 0;
  DiagnosticStream heapDiagnostics_;
  struct ReportPeer {
    IPAddress address;
    bool used = false;
  };
  ReportPeer reportPeers[8];
  uint8_t nextReportPeer = 0;
  uint32_t lastReportMs = 0;
  uint32_t heapBeforeBt_ = 0, heapAfterBt_ = 0, heapAfterAp_ = 0, heapAfterWifi_ = 0,
           heapAfterSetup_ = 0;
  HttpDiagnostics http_;
};
} // namespace forkrot
