// G5500 Fork; original by UA1CFM. GPL-3.0.
#include "network.hpp"
#include "network_addresses.hpp"
#include "wifi_memory.hpp"
#include "tcp_memory.hpp"
#include "runtime_metrics.hpp"
#include "crowpanel_board.hpp"
#include <esp_app_desc.h>
#include <esp_system.h>
#include <hal/uart_types.h>
#include <driver/uart.h>
#include <cstdio>
#include "write_budget.hpp"
#include <cerrno>
#include <new>
namespace forkrot {
using namespace board;
size_t Network::uartWrite(const uint8_t *data,size_t size) {
  if(modbus_ && runtime_.modbus.pc)return 0;
  return rs485Reply_?rs485Reply_->write(data,size):Serial0.write(data,size);
}
void Network::serviceUartTx() {
  if(!rs485Reply_)return;
  const int available=Serial0.availableForWrite();
  rs485Reply_->service(millis(),available>0?size_t(available):0,
      [](const uint8_t *p,size_t n) { return Serial0.write(p,n); });
}
void Network::serviceHeapDiagnostics() {
  heapDiagnostics_.service(millis(),
      [this](uint8_t page, char *reply, size_t capacity) {
        return formatHeapDiagnostics(page, reply, capacity);
      }, [this](DiagnosticStream::Port port, const char *data, size_t size) -> size_t {
        if (port == DiagnosticStream::Port::Bluetooth)
          return bluetooth_.send(std::string_view(data, size)) ? size : 0;
#if FORK_NATIVE_USB
        if(port==DiagnosticStream::Port::NativeUsb)return diagnostics_.nativeWrite(reinterpret_cast<const uint8_t *>(data),size);
#endif
        if(runtime_.controller.hardware.pins.uartTx<0 || (modbus_&&runtime_.modbus.pc)) return 0;
        const int available = Serial0.availableForWrite();
        if (available <= 0) return 0;
        const size_t count = size_t(available) < size ? size_t(available) : size;
        return uartWrite(reinterpret_cast<const uint8_t *>(data), count);
      });
}
void Network::reportAllocationFailures() {
  // Called after outputs are stopped and the recovery reserve is released.
  // Emit the captured caller before the automatic reboot clears RAM. This
  // path uses fixed buffers and never sends diagnostics onto a Modbus bus.
#if !FORK_NATIVE_USB
  if(runtime_.controller.hardware.pins.uartTx<0 || (modbus_&&runtime_.modbus.pc))return;
#endif
  static constexpr uint8_t newline[]={'\r','\n'};
#if FORK_NATIVE_USB
  diagnostics_.nativeWrite(newline,sizeof newline);
#else
  uartWrite(newline,sizeof newline);
#endif
  char line[256];
  for(uint8_t page=7;page<=8;++page) {
    const int size=formatHeapDiagnostics(page,line,sizeof line);
    if(size<=0)continue;
    const auto count=std::min(size_t(size),sizeof line-1);
#if FORK_NATIVE_USB
    diagnostics_.nativeWrite(reinterpret_cast<const uint8_t *>(line),count);
#else
    uartWrite(reinterpret_cast<const uint8_t *>(line),count);
#endif
  }
#if !FORK_NATIVE_USB
  Serial0.flush();
#endif
}
int Network::formatHeapDiagnostics(uint8_t page, char *reply, size_t capacity) {
  switch (page) {
  case 0: {
    char elf[13];
    const auto *app = esp_app_get_description();
    for (unsigned i = 0; i < 6; ++i)
      std::snprintf(elf + i * 2, sizeof elf - i * 2, "%02x", unsigned(app->app_elf_sha256[i]));
    return std::snprintf(reply, capacity,
        "HEAP free=%lu min=%lu max=%lu beforeBT=%lu afterBT=%lu afterAP=%lu afterWiFi=%lu setup=%lu elf=%s\r\n",
        static_cast<unsigned long>(heapFreeBytes()), static_cast<unsigned long>(heapMinBytes()),
        static_cast<unsigned long>(heapMaxBlockBytes()), static_cast<unsigned long>(heapBeforeBt_),
        static_cast<unsigned long>(heapAfterBt_), static_cast<unsigned long>(heapAfterAp_),
        static_cast<unsigned long>(heapAfterWifi_), static_cast<unsigned long>(heapAfterSetup_), elf);
  }
  case 1:
    return std::snprintf(reply, capacity,
        "WIFI status=%d savedSSID=%s NVS=%s BTready=%s BTclient=%s BTlink=%s profile=%s\r\n",
        int(WiFi.status()), credentials_.wifiSsid.length() ? "yes" : "no",
        diagnostics_.wifiCredentialsReadOk() ? "OK" : "unavailable",
        btReady_ ? "yes" : "no", bluetooth_.connected() ? "yes" : "no",
        bluetooth_.linkConnected() ? "yes" : "no",
        compactWifi_ ? "compact" : bufferedWifi_ ? (FORK_CLASSIC_ESP32 ? "controller" : "buffered") : "standard");
  case 2: {
    const IPAddress ap = (WiFi.getMode() & WIFI_AP) ? WiFi.softAPIP() : IPAddress();
    const IPAddress sta = WiFi.localIP();
    return std::snprintf(reply, capacity, "IP ap=%u.%u.%u.%u sta=%u.%u.%u.%u rssi=%d\r\n",
        unsigned(ap[0]), unsigned(ap[1]), unsigned(ap[2]), unsigned(ap[3]),
        unsigned(sta[0]), unsigned(sta[1]), unsigned(sta[2]), unsigned(sta[3]), int(WiFi.RSSI()));
  }
  case 3:
    return std::snprintf(reply, capacity,
        "HTTP accepted=%lu rxFailures=%lu rejected=%lu txFailures=%lu cause=%s errno=%d bytes=%lu/%lu requests=%lu\r\n",
        static_cast<unsigned long>(http_.accepted),
        static_cast<unsigned long>(http_.readFailures), static_cast<unsigned long>(http_.rejected),
        static_cast<unsigned long>(http_.writeFailures), http_.writeCause, http_.writeErrno,
        static_cast<unsigned long>(http_.writeBytes), static_cast<unsigned long>(http_.writeSize),
        static_cast<unsigned long>(http_.requests));
  case 4: {
    const auto tcp = tcpMemory();
    return std::snprintf(reply, capacity, "TCP stats=%s active=%u closing=%u timewait=%u pcbBytes=%u livePeers=%u unsent=%lu unacked=%lu buf=%lu wnd=%lu cwnd=%lu retry=%u\r\n",
        tcp.available ? "OK" : "unavailable", unsigned(tcp.active), unsigned(tcp.closing),
        unsigned(tcp.timewait), unsigned(tcp.pcbBytes),unsigned(tcp.livePeers),
        static_cast<unsigned long>(tcp.liveUnsent),static_cast<unsigned long>(tcp.liveUnacked),
        static_cast<unsigned long>(tcp.liveBuffer),static_cast<unsigned long>(tcp.liveWindow),
        static_cast<unsigned long>(tcp.liveCwnd),unsigned(tcp.liveRetries));
  }
  case 5: {
    const auto mode = WiFi.getMode();
    return std::snprintf(reply, capacity, "WIFI runtimeMode=%s apClients=%u apPolicy=%s ethLink=%s ethIp=%s\r\n",
                         mode == WIFI_STA      ? "STA"
                         : mode == WIFI_AP     ? "AP"
                         : mode == WIFI_AP_STA ? "AP+STA"
                                               : "OFF",
                         (mode & WIFI_AP) ? unsigned(WiFi.softAPgetStationNum()) : 0,
                         apAlwaysOn() ? "always" : "fallback",
                         ethernetLinkUp()?"up":"down",ethernetIP().toString().c_str());
  }
  case 6:
    return std::snprintf(reply, capacity,
        "RUNTIME allocHook=%s allocFailures=%lu maxFailed=%lu loopMaxUs=%lu loopsOver20ms=%lu stackMin=%lu btRxDropped=%lu\r\n",
        runtimeMetrics.hookReady ? "OK" : "unavailable",
        static_cast<unsigned long>(runtimeMetrics.allocationFailures.load()),
        static_cast<unsigned long>(runtimeMetrics.largestFailedAllocation.load()),
        static_cast<unsigned long>(runtimeMetrics.loopMaxUs),
        static_cast<unsigned long>(runtimeMetrics.loopsOver20ms),
        static_cast<unsigned long>(uxTaskGetStackHighWaterMark(nullptr)),
        static_cast<unsigned long>(bluetooth_.droppedCommands()));
  case 7: case 8: {
    const auto &failure = runtimeMetrics.failures[page - 7];
    if (!failure.ready()) return 0;
    return std::snprintf(reply, capacity,
        "OOM size=%lu caps=%lx free=%lu max=%lu task=%s fn=%s pc=%08lx,%08lx,%08lx,%08lx,%08lx,%08lx,%08lx,%08lx\r\n",
        static_cast<unsigned long>(failure.size), static_cast<unsigned long>(failure.caps),
        static_cast<unsigned long>(failure.freeBytes), static_cast<unsigned long>(failure.largestBlock),
        failure.task, failure.function,
        static_cast<unsigned long>(failure.pc[0]), static_cast<unsigned long>(failure.pc[1]),
        static_cast<unsigned long>(failure.pc[2]), static_cast<unsigned long>(failure.pc[3]),
        static_cast<unsigned long>(failure.pc[4]), static_cast<unsigned long>(failure.pc[5]),
        static_cast<unsigned long>(failure.pc[6]), static_cast<unsigned long>(failure.pc[7]));
  }
  default: {
    constexpr uint32_t psramCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    const auto total = heap_caps_get_total_size(psramCaps);
    return std::snprintf(reply, capacity, "PSRAM available=%s total=%lu free=%lu min=%lu max=%lu\r\n",
        total ? "yes" : "no", static_cast<unsigned long>(total),
        static_cast<unsigned long>(heap_caps_get_free_size(psramCaps)),
        static_cast<unsigned long>(heap_caps_get_minimum_free_size(psramCaps)),
        static_cast<unsigned long>(heap_caps_get_largest_free_block(psramCaps)));
  }
  }
}
void Network::sendReply(const String &s) {
#if FORK_NATIVE_USB
  if (gsReplyPort == GS_REPLY_NATIVE_USB) {
    diagnostics_.nativeWrite(reinterpret_cast<const uint8_t *>(s.c_str()),s.length());
    hardware_.flashTx();
    return;
  }
#endif
  if (gsReplyPort == GS_REPLY_BLUETOOTH) {
    if (!bluetooth_.send(s)) diagnostics_.dbgPrintln("[BT TX] dropped: disconnected or queue full");
    diagnostics_.dbgPrint("[BT TX] ");
    diagnostics_.dbgPrintln(s);
  } else {
    if(runtime_.controller.hardware.pins.uartTx>=0) uartWrite(reinterpret_cast<const uint8_t *>(s.c_str()),s.length());
    diagnostics_.dbgPstTx(s);
  }
  hardware_.flashTx();
}

void Network::processGs232Command(std::string_view input) {
  if(processCommandApi(input,uint8_t(gsReplyPort)))return;
  if(!runtime_.recovery)actions_.updatePosition();
  auto packet = runtime_.protocol.line(input, millis());
  if (packet.matched) {
    if (packet.result == forkrot::Result::Ok || packet.result == forkrot::Result::Pending) {
      runtime_.targetValid = true;
      runtime_.lastTargetRxMs = millis();
    }
    runtime_.controller.tick(millis());
    hardware_.applyOutputs();
    if (packet.result == Result::Deferred && deferReply(packet, uint8_t(gsReplyPort)))
      return;
    if (!packet.reply.empty()) {
      sendReply(packet.reply.c_str());
    }
    return;
  }

  if (input.size() > 32) return;
  String cmd(input.data(), input.size());
  cmd.trim();
  if (!cmd.length()) {
    return;
  }
  cmd.toUpperCase();
  hardware_.flashRx();

  // GS-232 azimuth operating range. This also defines the full-scale angle
  // captured by F in the GS232 2-POINT calibration mode.
  if (runtime_.hud.isHud() && (cmd == "P36" || cmd == "P45" || cmd == "O" || cmd == "F" || cmd == "O2" ||
                               cmd == "F2" || cmd.startsWith("SIM "))) {
    sendReply("?>\r\n");
    return;
  }
  if (cmd == "P36") {
    actions_.setGsAzMode(36);
    return;
  }
  if (cmd == "P45") {
    actions_.setGsAzMode(45);
    return;
  }

  // Standard GS-232 calibration commands.
  // O  = current AZ feedback becomes the active range start
  // F  = current AZ feedback becomes the active range end
  // O2 = current EL feedback becomes the active range start
  // F2 = current EL feedback becomes the active range end
  if (cmd == "O" || cmd == "O2" || cmd == "F" || cmd == "F2") {
    actions_.captureGs232Cal(cmd);
    return;
  }

  // Basic help response. Z is intentionally not implemented.
  if (cmd == "H") {
    sendReply("GS-232B: R L A C M S O F P36 P45\r\n");
    return;
  }
  if (cmd == "H2") {
    sendReply("GS-232B: U D E C2 W S O2 F2 B\r\n");
    return;
  }
  if (cmd == "H3") {
    sendReply(String("CAL=") + calibration_.calibrationModeName() + "\r\n");
    return;
  }
  if (cmd == "H4") {
    heapDiagnostics_.start(gsReplyPort == GS_REPLY_NATIVE_USB ? DiagnosticStream::Port::NativeUsb : gsReplyPort == GS_REPLY_USB ? DiagnosticStream::Port::Usb
                                                      : DiagnosticStream::Port::Bluetooth, millis());
    return;
  }
#if SELECTED_BOARD == BOARD_CROWPANEL_21
  if (runtime_.controller.hardware.simulation && gsReplyPort == GS_REPLY_NATIVE_USB && cmd.startsWith("SIM ")) {
    float az = 0, el = 0; char extra;
    const auto result = std::sscanf(cmd.c_str(), "SIM %f %f %c", &az, &el, &extra) == 2
        ? runtime_.controller.target({az, el, true, true}, false, millis()) : Result::Invalid;
    hardware_.controlRotator();
    sendReply(String("SIM ") + Controller::resultName(result) + "\r\n");
    return;
  }
  if (cmd == "H5") {
    const auto stats = crowpanel::displayStats();
    char reply[384];
    std::snprintf(reply, sizeof reply,
                  "CROW ms=%lu az=%.3f el=%.3f targetAz=%.3f targetEl=%.3f moving=%d sim=%d inv=%d vsync=%lu "
                  "frames=%lu waitMaxUs=%lu timeouts=%lu failed=%d restarts=%lu restartMask=%lu restartErrors=%lu "
                  "vsyncMinUs=%lu vsyncMaxUs=%lu\r\n",
                  (unsigned long)millis(), double(runtime_.controller.axes[0].current),
                  double(runtime_.controller.axes[1].current), double(runtime_.controller.axes[0].target),
                  double(runtime_.controller.axes[1].target), int(runtime_.controller.moving()),
                  int(!runtime_.hud.isHud() && runtime_.controller.hardware.simulation),
                  int(runtime_.loggerSettings.encoderInverted), (unsigned long)stats.vsyncs,
                  (unsigned long)stats.frames, (unsigned long)stats.waitMaxUs, (unsigned long)stats.timeouts,
                  int(stats.failed), (unsigned long)stats.restarts, (unsigned long)stats.restartBufferMask,
                  (unsigned long)stats.restartErrors, (unsigned long)stats.vsyncMinUs, (unsigned long)stats.vsyncMaxUs);
    sendReply(reply);
    return;
  }
#endif
}

void Network::handlePstRotatorSerial() {
#if FORK_NATIVE_USB
  static ApiLineBuffer usbReceiver;
  static uint32_t usbEpoch=0;
  if(usbEpoch!=runtime_.hud.selectionEpoch) {
    usbEpoch=runtime_.hud.selectionEpoch;usbReceiver.cancelPending();
    while(diagnostics_.nativeAvailable())usbReceiver.discard(char(diagnostics_.nativeRead()));
    usbReceiver.cancelPending();
  }
  std::string_view usbCommand;
  const uint32_t usbStarted=millis();unsigned usbCommands=0;
  for(unsigned bytes=0;bytes<128 && usbCommands<4 && diagnostics_.nativeAvailable() && uint32_t(millis()-usbStarted)<2;++bytes) {
    if(usbReceiver.feed(char(diagnostics_.nativeRead()),usbCommand)) {
      ++usbCommands;gsReplyPort=GS_REPLY_NATIVE_USB;processGs232Command(usbCommand);
    }
  }
#endif
  if(modbus_ && runtime_.modbus.pc){serviceModbusPc();return;}
  if ((runtime_.hud.config.uart && !runtime_.controller.hardware.hudRs485()) || runtime_.controller.hardware.pins.uartRx < 0 || runtime_.controller.hardware.pins.uartTx < 0)
    return;
  static forkrot::ApiLineBuffer receiver;
  static uint32_t serialEpoch = 0;
  if (serialEpoch != runtime_.hud.selectionEpoch) {
    serialEpoch = runtime_.hud.selectionEpoch;
    receiver.cancelPending();
    while (Serial0.available())
      receiver.discard(char(Serial0.read()));
    receiver.cancelPending();
  }
  std::string_view command;
  const uint32_t started = millis();
  unsigned commands = 0;
  for (unsigned bytes = 0; bytes < 128 && commands < 4 && Serial0.available() && uint32_t(millis() - started) < 2; ++bytes) {
    const char byte=char(Serial0.read());
    if(rs485Reply_)rs485Reply_->received(millis());
    if (receiver.feed(byte, command)) {
      ++commands;
      gsReplyPort = GS_REPLY_USB;
      if(command.empty() || command.front()!='@') {diagnostics_.dbgView("[PST RX] "); diagnostics_.dbgView(command, true);}
      processGs232Command(command);
    }
  }
  serviceUartTx();
}

void Network::handleBluetoothSerial() {
  uint32_t dropped = bluetooth_.droppedCommands();
  if (dropped != lastBtDropped_) {
    diagnostics_.dbgPrintln("[BT RX] dropped complete commands: queue capacity");
    lastBtDropped_ = dropped;
  }
  char bytes[512]; size_t size;
  const uint32_t started = millis();
  for (unsigned count = 0; count < 4 && uint32_t(millis() - started) < 2 && bluetooth_.receive(bytes, sizeof bytes, size); ++count) {
    gsReplyPort = GS_REPLY_BLUETOOTH;
    std::string_view command(bytes, size);
    diagnostics_.dbgView("[BT RX] "); diagnostics_.dbgView(command, true);
    processGs232Command(command);
  }
}

void Network::rotctldSend(const String &s) {
  bool sent = false;
  if (rotctldClient_ && rotctldClient_.connected()) {
    const int socket = rotctldClient_.fd();
    const auto written = writeWithinBudget(s.c_str(), s.length(), millis(), 5,
        [socket](const char *data, size_t size) {
          int count = ::send(socket, data, size, MSG_DONTWAIT);
          if (count >= 0) return count;
          return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR || errno == ENOMEM || errno == ENOBUFS ? -2 : -1;
        }, [] { return millis(); }, [] { delay(1); });
    sent = written == s.length();
    if (sent) hardware_.flashTx();
    else rotctldClient_.stop(); // no partial reply carried into the next session
  }
#if FORK_NATIVE_USB
  if (sent) {
    diagnostics_.dbgPrint("[TCP TX] ");
    String t = s;
    t.replace("\r", "");
    t.replace("\n", " | ");
    diagnostics_.dbgPrintln(t);
  }
#endif
}

void Network::processRotctldCommand(std::string_view input) {
  if(processCommandApi(input,3))return;
  actions_.updatePosition();
  auto packet = runtime_.protocol.line(input, millis(), true);
  if (packet.matched) {
    if (packet.result == forkrot::Result::Ok || packet.result == forkrot::Result::Pending) {
      runtime_.targetValid = true;
      runtime_.lastTargetRxMs = millis();
    }
    runtime_.controller.tick(millis());
    hardware_.applyOutputs();
    if (packet.result == Result::Deferred && deferReply(packet, 3))
      return;
    if (!packet.reply.empty()) {
      rotctldSend(packet.reply.c_str());
    }
    return;
  }

  if (input.size() > 32) return;
  String cmd(input.data(), input.size());
  cmd.trim();
  if (!cmd.length()) {
    return;
  }

  hardware_.flashRx();

#if FORK_NATIVE_USB
  diagnostics_.dbgPrint("[TCP RX] ");
  diagnostics_.dbgPrintln(cmd);
#endif

  actions_.updatePosition();

  String lower = cmd;
  lower.toLowerCase();

  String upper = cmd;
  upper.toUpperCase();
  if (runtime_.hud.isHud() &&
      (upper == "P36" || upper == "P45" || upper == "O" || upper == "F" || upper == "O2" || upper == "F2")) {
    rotctldSend("RPRT -1\n");
    return;
  }
  if (upper == "P36") {
    actions_.setGsAzMode(36);
    return;
  }
  if (upper == "P45") {
    actions_.setGsAzMode(45);
    return;
  }

  // Standard GS-232 two-point calibration commands.
  if (upper == "O" || upper == "O2" || upper == "F" || upper == "F2") {
    actions_.captureGs232Cal(upper);
    return;
  }

  if (upper == "H") {
    rotctldSend("R L A C M T N S O F X1 X2 X3 X4\r\n");
    return;
  }
  if (upper == "H2") {
    rotctldSend("U D E C2 W T N S O2 F2 B\r\n");
    return;
  }
  if (upper == "H3") {
    String s = "P45 P36 MODE=";
    if (calibration_.state().gsAzMode == AZ_CUSTOM)
      s += String("CUSTOM ") + String(runtime_.controller.minimum,1) + ".." + String(runtime_.controller.maximum,1);
    else s += (calibration_.state().gsAzMode == 45 ? "450" : "360");
    s += " CAL=";
    s += calibration_.calibrationModeName();
    s += "\r\n";
    rotctldSend(s);
    return;
  }

  // Quit current network session.
  if (cmd == "q" || cmd == "Q" || lower == "\\quit" || lower == "quit") {
    rotctldSend("RPRT 0\n");
    rotctldClient_.stop();
    return;
  }

  // Minimal info support.
  if (cmd == "_" || lower == "\\get_info" || lower == "get_info") {
    rotctldSend("G5500 " FORK_CHIP_NAME " v1.6.23 UA1CFM Look4Sat bridge\n");
    return;
  }

  // Some clients probe capabilities/help. Do not tear down the session.
  if (cmd == "?" || lower == "\\dump_caps" || lower == "dump_caps") {
    rotctldSend("RPRT 0\n");
    return;
  }

#if FORK_NATIVE_USB
  diagnostics_.dbgPrint("[TCP UNKNOWN] ");
  diagnostics_.dbgPrintln(cmd);
#endif
  rotctldSend("RPRT -1\n");
}

void Network::handleRotctldTcp() {
  // Accept a client if none is currently connected.
  if (!rotctldClient_ || !rotctldClient_.connected()) {
    WiFiClient c = tcpServer_.accept();
    if (c) {
      if (rotctldClient_) {
        rotctldClient_.stop();
      }
      rotctldClient_ = c;
      rotctldClient_.setNoDelay(true);
      ++tcpSession_;
      tcpReceiver_.clear();
#if FORK_NATIVE_USB
      diagnostics_.dbgPrintln("[TCP] Look4Sat/rotctld client connected");
#endif
    } else {
      return;
    }
  }

  std::string_view command;
  const uint32_t started = millis();
  unsigned commands = 0;
  for (unsigned bytes = 0; bytes < 128 && commands < 4 && rotctldClient_ && uint32_t(millis() - started) < 2; ++bytes) {
    char byte;
    const int count = ::recv(rotctldClient_.fd(), &byte, 1, MSG_DONTWAIT);
    if (count != 1) {
      if (count == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) rotctldClient_.stop();
      break;
    }
    if (tcpReceiver_.feed(byte, command)) { ++commands; processRotctldCommand(command); }
  }
}

bool Network::configureStation() {
  const auto &c=credentials_.network.wifi;
  const auto ip=[](uint32_t n){return IPAddress(uint8_t(n>>24),uint8_t(n>>16),uint8_t(n>>8),uint8_t(n));};
  const bool ok=c.automatic?WiFi.config(ip(0),ip(0),ip(0)):WiFi.config(ip(c.address),ip(c.gateway),ip(c.mask),ip(c.dns1),ip(c.dns2));
  if(!ok)diagnostics_.dbgPrintln("[WIFI] IP configuration failed; setup AP remains available");
  return ok;
}
bool Network::startWifi() {
  if (ETHERNET_SUPPORTED && !ethernet_.begin(credentials_.network.ethernet))
    diagnostics_.dbgPrintln("[ETH] PHY start failed; Wi-Fi remains available");
  WiFi.persistent(false);
  if (runtime_.recovery || (FORK_CLASSIC_ESP32 && runtime_.controller.hardware.backend() == BT_ESP32_SPP)) {
    compactWifi_ = initCompactWifi() == ESP_OK;
    if (!compactWifi_) return false; // never fall back to a larger buffer profile on OOM
  } else if (FORK_CLASSIC_ESP32) {
    bufferedWifi_ = initControllerWifi() == ESP_OK;
    if (!bufferedWifi_) return false;
  } else if (SELECTED_BOARD == BOARD_CROWPANEL_21) {
    bufferedWifi_ = initBufferedWifi() == ESP_OK;
    if (!bufferedWifi_) return false;
  }
  WiFi.setSleep(false);
  // The policy below owns retries; Arduino's event callback must not launch
  // an independent reconnect after we deliberately return to AP-only mode.
  WiFi.setAutoReconnect(false);
  if (apAlwaysOn())
    restoreApOnlyAfterReconnectFail();
  if (!runtime_.recovery && credentials_.wifiSsid.length() && runtime_.hud.config.wifi != HudWifi::Host) {
    diagnostics_.dbgPrintln(apAlwaysOn() ? "[WIFI] starting STA; AP remains available"
                                         : "[WIFI] starting STA; AP reserved for fallback");
    if (WiFi.mode(apAlwaysOn() ? WIFI_AP_STA : WIFI_STA)) {
      if(configureStation())WiFi.begin(credentials_.wifiSsid.c_str(), credentials_.wifiPass.c_str());
      const uint32_t started = millis();
      while (WiFi.status() != WL_CONNECTED && uint32_t(millis() - started) < WifiFallback::AttemptMs)
        delay(100);
    }
  }
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected) {
    diagnostics_.dbgPrint(apAlwaysOn() ? "[WIFI] STA connected, AP remains, IP = "
                                       : "[WIFI] STA connected, AP off, IP = ");
    diagnostics_.dbgPrintln(WiFi.localIP().toString());
  } else if (!apAlwaysOn() || WiFi.getMode() != WIFI_AP) {
    restoreApOnlyAfterReconnectFail();
  }
  serviceApSubnet();
  wifiFallback_.reset(millis(), connected);
  setupCommandUdp();
  if (!runtime_.recovery) {
  if (!hudPortValid(runtime_.hud.config.port, runtime_.loggerSettings.n1mmPort,
                    runtime_.loggerSettings.pstPort, runtime_.loggerSettings.reportPort) ||
      !hudUdp_.begin(runtime_.hud.config.port))
    diagnostics_.dbgPrintln("[HUD] UDP port unavailable");
  else if (!hudUdp_.setIpPrecedence(4))
    diagnostics_.dbgPrintln("[HUD] UDP priority unavailable; using default queue");
  tcpServer_.begin();
  }
  actions_.setupWeb();
  heapAfterWifi_ = heapFreeBytes();
  diagnostics_.dbgPrintln(apAlwaysOn() ? "[WIFI] AP always on; STA retry 30s, attempt 8s"
                                       : "[WIFI] STA preferred; AP fallback, retry 30s, attempt 8s");
  return runtime_.recovery ? (WiFi.getMode() == WIFI_AP) : true;
}

void Network::restoreApOnlyAfterReconnectFail() {
  const IPAddress apIP = wifiSetupAp_ && (WiFi.getMode() & WIFI_AP)
                             ? WiFi.softAPIP() : IPAddress(192, 168, 4, 1);
  // No driver deinit/reinit: keep the compact buffer configuration. The mode
  // transition stops the failed STA and releases its connection state.
  if (WiFi.getMode() & WIFI_STA) WiFi.disconnect(false, false);
  if (!WiFi.mode(WIFI_AP)) {
    diagnostics_.dbgPrintln("[WIFI] AP mode failed");
    return;
  }
  const IPAddress apMask(255, 255, 255, 0);
  char apName[33];
  std::snprintf(apName, sizeof apName, "G5500-%s-%06llX", runtime_.hud.isHud() ? "HUD" : "Rotator",
                static_cast<unsigned long long>(runtime_.hud.id & 0xffffff));
  const bool configured = WiFi.softAPConfig(apIP, apIP, apMask);
  const bool started = credentials_.apPass.length() >= 8 ? WiFi.softAP(apName, credentials_.apPass.c_str())
                                                         : WiFi.softAP(apName);
  heapAfterAp_ = heapFreeBytes(); // zero means AP has not been started this boot
  diagnostics_.dbgPrintln(configured && started ? String("[WIFI] AP ready at ") + apIP.toString()
                                               : String("[WIFI] AP start failed"));
}

void Network::serviceApSubnet() {
  // The setup browser still uses the original AP address. Defer an overlap
  // adjustment until reboot so DHCP success cannot move that page out of reach.
  if (wifiSetupAp_ || (!ethernetConnected() && WiFi.status() != WL_CONNECTED) || !(WiFi.getMode() & WIFI_AP))
    return;
  const bool wifi=WiFi.status()==WL_CONNECTED;
  const uint32_t sta=wifi?uint32_t(WiFi.localIP()):0, mask=wifi?uint32_t(WiFi.subnetMask()):0;
  uint32_t eth=0,ethMask=0;
#if SELECTED_BOARD == BOARD_WT32_ETH01 || SELECTED_BOARD == BOARD_WAVESHARE_S3_ETH
  if(ethernetConnected()) { eth=uint32_t(ETH.localIP());ethMask=uint32_t(ETH.subnetMask()); }
#endif
  const uint32_t ap=uint32_t(WiFi.softAPIP());
  if(!apSubnetOverlaps(ap,sta,mask) && !apSubnetOverlaps(ap,eth,ethMask)) return;
  const uint32_t chosen=serviceApAddress(sta,mask,eth,ethMask);
  if (chosen && WiFi.softAPConfig(IPAddress(chosen), IPAddress(chosen), IPAddress(255, 255, 255, 0))) {
    diagnostics_.dbgPrint("[WIFI] AP subnet changed to avoid uplink overlap: ");
    diagnostics_.dbgPrintln(WiFi.softAPIP().toString());
  }
}

void Network::formatAddresses(char *text, size_t capacity, const char *separator) const {
  char sta[16]{}, ap[16]{}, eth[16]{};
  const auto address = [](char *out, const IPAddress &ip) {
    std::snprintf(out, 16, "%u.%u.%u.%u", unsigned(ip[0]), unsigned(ip[1]),
                  unsigned(ip[2]), unsigned(ip[3]));
  };
  if (WiFi.status() == WL_CONNECTED) address(sta, WiFi.localIP());
  if (WiFi.getMode() & WIFI_AP) address(ap, WiFi.softAPIP());
  if (ethernetConnected()) address(eth, ethernetIP());
  formatNetworkAddresses(text, capacity, sta, ap, separator, eth);
}

void Network::connectWifi(const String &ssid, const String &password) {
  credentials_.wifiSsid = ssid;
  credentials_.wifiPass = password;
  // Keep the setup browser reachable until the next reboot, including on ESP32.
  wifiSetupAp_ = true;
  wifiConnectPending_ = true;
  wifiConnectRequestedMs_ = millis();
}

void Network::serviceWifiReconnect() {
  const bool wired=ethernetConnected();
  if(wired!=ethernetWasConnected_) {
    ethernetWasConnected_=wired;
    diagnostics_.dbgPrintln(wired ? String("[ETH] IP = ")+ethernetIP().toString()
                                : String("[ETH] disconnected or waiting for DHCP"));
  }
  if (wifiConnectPending_) {
    // Finish sending the save response before changing the radio channel.
    if (uint32_t(millis() - wifiConnectRequestedMs_) < 250) return;
    wifiConnectPending_ = false;
    if (!(WiFi.getMode() & WIFI_AP)) restoreApOnlyAfterReconnectFail();
    WiFi.disconnect(false, false);
    if (WiFi.mode(WIFI_AP_STA))
      if(configureStation())WiFi.begin(credentials_.wifiSsid.c_str(), credentials_.wifiPass.c_str());
    wifiFallback_.beginAttempt(millis());
    return;
  }
  serviceApSubnet();
  if (runtime_.hud.config.wifi == HudWifi::Host)
    return;
  const auto action =
      wifiFallback_.update(millis(), credentials_.wifiSsid.length() != 0, WiFi.status() == WL_CONNECTED,
                           (WiFi.getMode() & WIFI_AP) != 0, apAlwaysOn());
  if (action == WifiAction::StopAp) {
    if (WiFi.mode(WIFI_STA)) diagnostics_.dbgPrintln("[WIFI] STA connected; fallback AP stopped");
    else diagnostics_.dbgPrintln("[WIFI] failed to stop fallback AP");
  } else if (action == WifiAction::StartAp) {
    restoreApOnlyAfterReconnectFail();
  } else if (action == WifiAction::ReconnectSta) {
    diagnostics_.dbgPrintln("[WIFI] STA lost; immediate reconnect before fallback");
    if (!WiFi.reconnect()) diagnostics_.dbgPrintln("[WIFI] immediate STA reconnect failed");
  } else if (action == WifiAction::StartSta) {
    diagnostics_.dbgPrintln("[WIFI] STA reconnect attempt; AP remains during attempt");
    if (WiFi.mode(WIFI_AP_STA))
      if(configureStation())WiFi.begin(credentials_.wifiSsid.c_str(), credentials_.wifiPass.c_str());
  }
}

void Network::setupLoggerUdp() {
  n1mmUdp.stop();
  pstUdp.stop();
  bool a = n1mmUdp.begin(runtime_.loggerSettings.n1mmPort),
       b = pstUdp.begin(runtime_.loggerSettings.pstPort);
  if (!a || !b) {
    runtime_.controller.fault = "udp_bind_failed";
  }
  for (auto &peer : reportPeers) {
    peer.used = false;
  }
}

void Network::serviceLoggerUdp() {
  const uint32_t started = millis();
  for (DatagramSocket *socket : {&n1mmUdp, &pstUdp}) {
    for (unsigned count = 0; count < 2 && uint32_t(millis() - started) < 2; ++count) {
      Datagram received;
      const auto result = socket->receive(received);
      if (result == DatagramResult::Empty) break;
      if (result == DatagramResult::Dropped) continue;
      const IPAddress sender(received.address);
      const auto text = received.text();
      auto packet = runtime_.protocol.datagram(text, millis());
      if (packet.result == forkrot::Result::Invalid) {
        diagnostics_.dbgPrintln("[UDP] rejected invalid packet");
        continue;
      }
      if (packet.matched) {
        hardware_.flashRx();
        runtime_.lastTargetRxMs = millis();
        if (packet.result == forkrot::Result::Ok || packet.result == forkrot::Result::Pending) {
          runtime_.targetValid = true;
        }
      }
      if (packet.matched && text.find("N1MMRotor") != std::string_view::npos) {
        bool known = false;
        for (auto peer : reportPeers) {
          if (peer.used && peer.address == sender) {
            known = true;
          }
        }
        if (!known) {
          reportPeers[nextReportPeer] = {sender, true};
          nextReportPeer = (nextReportPeer + 1) % 8;
        }
      }
      if (!packet.reply.empty()) {
        if (socket->send(uint32_t(sender), runtime_.loggerSettings.pstPort + 1, packet.reply))
          hardware_.flashTx();
      }
    }
  }
  if (runtime_.controller.available() && uint32_t(millis() - lastReportMs) >= 500) {
    lastReportMs = millis();
    const auto message = runtime_.protocol.positionReport(runtime_.currentAz);
    for (auto peer : reportPeers) {
      if (peer.used) {
        n1mmUdp.send(uint32_t(peer.address), runtime_.loggerSettings.reportPort, message);
      }
    }
    IPAddress host;
    if (!runtime_.loggerSettings.reportHost.empty() &&
        host.fromString(runtime_.loggerSettings.reportHost.c_str()) && host.type() == IPv4) {
      n1mmUdp.send(uint32_t(host), runtime_.loggerSettings.reportPort, message);
    }
  }
}
void Network::serviceModbusPc() {
  if(!modbus_||!runtime_.modbus.pc)return;
  auto &m=*modbus_;mb::Frame frame;
  for(unsigned i=0;i<2&&m.pc.read(frame);++i) {
    if(!frame.data[0]||frame.data[0]>247||uint32_t(micros()-frame.at)>80000){++m.pcRejected;continue;}
    ++m.pcReceived;
    if(m.pcResponse.size){++m.pcRejected;continue;} // One outstanding PC request.
    if(m.busReady&&runtime_.hud.isHud())m.link.pcRequest(frame,micros());
    else if(frame.data[0]==runtime_.modbus.address) {
      if(!runtime_.recovery)actions_.updatePosition();
      m.pcResponse=m.link.registers().process(frame,millis());m.pcResponse.at=micros();hardware_.applyOutputs();
    }
  }
  if(!m.pcResponse.size&&m.link.pcReply(m.pcResponse))m.pcResponse.at=micros();
  if(m.pcResponse.size&&uint32_t(micros()-m.pcResponse.at)>80000){m.pcResponse.size=0;++m.pcTimeouts;}
  if(m.pcResponse.size&&m.pc.send(m.pcResponse))m.pcResponse.size=0;
  runtime_.modbusStatus.received=m.link.received+m.pcReceived;
  runtime_.modbusStatus.rejected=m.link.rejectedFrames()+m.pcRejected+m.bus.rejected()+m.pc.rejected();
  runtime_.modbusStatus.timeouts=m.link.timeouts+m.pcTimeouts;
}
void Network::serviceModbusBus() {
  if(!modbus_||!modbus_->busReady)return;
  auto &m=*modbus_;mb::Frame frame;
  for(unsigned i=0;i<4&&m.bus.read(frame);++i) {
    if(!runtime_.recovery)actions_.updatePosition();
    m.link.receive(frame,micros(),millis(),[&](const mb::Frame &f){return m.bus.send(f);},
      [&](uint8_t address,const uint8_t *p,size_t n,uint32_t at){runtime_.hud.receive({address,runtime_.hud.config.port,true},p,n,at);});
    hardware_.applyOutputs();
  }
  m.link.service(micros(),millis(),m.bus.idle(),[&](const mb::Frame &f){return m.bus.send(f);});
  runtime_.modbusStatus={m.link.received+m.pcReceived,m.link.rejectedFrames()+m.pcRejected+m.bus.rejected()+m.pc.rejected(),m.link.timeouts+m.pcTimeouts,m.link.passes(),m.link.recoveries(),m.link.owner(),m.link.fault()};
}
bool Network::beginSerial() {
  const auto &config = runtime_.controller.hardware;
  if(!runtime_.recovery && (runtime_.modbus.pc||runtime_.modbus.bus) && !modbus_)return false;
  if(!runtime_.recovery && runtime_.modbus.wiring(config,runtime_.hud.config.uart))return false;
  if(modbus_ && runtime_.modbus.pc) {
    if(!modbus_->pc.begin(0,config.pins.uartRx,config.pins.uartTx,config.pins.uartDe,19200))return false;
  } else if (config.pins.uartRx >= 0 && config.pins.uartTx >= 0) {
    if (config.rs485()) {
      if(heapFreeBytes()<sizeof(Rs485Reply)+MemoryGuard::CriticalFree ||
          heapMaxBlockBytes()<sizeof(Rs485Reply)+MemoryGuard::CriticalBlock) return false;
      rs485Reply_.reset(new(std::nothrow) Rs485Reply);
      if(!rs485Reply_)return false;
      // Receive until the driver owns a complete transmission. RTS is released
      // by IDF after the last stop bit, rather than when write() returns.
      digitalWrite(config.pins.uartDe, LOW);
      pinMode(config.pins.uartDe, OUTPUT);
      Serial0.setTxBufferSize(2048);
    }
    if (runtime_.hud.config.uart && !config.hudRs485()) {
      if (config.pins.hudReady>=0) pinMode(config.pins.hudReady,INPUT);
    }
    Serial0.begin(runtime_.hud.config.uart && !config.hudRs485() ? runtime_.hud.config.link.baud : 9600, SERIAL_8N1, config.pins.uartRx, config.pins.uartTx);
    if (config.rs485() && (!Serial0.setPins(-1, -1, -1, config.pins.uartDe) ||
        !Serial0.setMode(UART_MODE_RS485_HALF_DUPLEX))) return false;
  }
  if(modbus_ && runtime_.modbus.bus) {
    if(!modbus_->bus.begin(1,config.pins.hudRx,config.pins.hudTx,config.pins.hudDe,115200) ||
        !modbus_->link.begin(runtime_.modbus,runtime_.hud.isHud(),uint32_t(runtime_.hud.boot),micros()))return false;
    modbus_->busReady=true;
  } else if (runtime_.hud.config.uart && config.hudRs485()) {
    if (!hud485_) return false;
    auto &serial=hud485_->serial;
    digitalWrite(config.pins.hudDe, LOW);pinMode(config.pins.hudDe, OUTPUT);
    serial.setRxBufferSize(2048);serial.setTxBufferSize(2048);
    serial.begin(115200, SERIAL_8N1, config.pins.hudRx, config.pins.hudTx);
    if (!serial.setPins(-1,-1,-1,config.pins.hudDe) || !serial.setMode(UART_MODE_RS485_HALF_DUPLEX)) return false;
  }
  heapBeforeBt_ = heapFreeBytes();
  const auto backend = runtime_.recovery ? BT_NONE : runtime_.controller.hardware.backend();
  if (backend == BT_ESP32_SPP && (heapFreeBytes() < 98304 || heapMaxBlockBytes() < 32768)) return false;
  btReady_ = bluetooth_.begin(backend,runtime_.controller.hardware.pins);
  if (!btReady_) diagnostics_.dbgPrintln("[BT] initialization failed; USB and network remain available");
  heapAfterBt_ = heapFreeBytes();
  return btReady_;
}

} // namespace forkrot

namespace forkrot {
bool Network::hudPortValid(unsigned port, unsigned n1mm, unsigned pst, unsigned report) const {
  return portsValid(port,n1mm,pst,report,runtime_.loggerSettings.commandPort);
}
void Network::beginHud() {
  runtime_.hud.begin(ESP.getEfuseMac() & 0xffffffffffffULL, (uint64_t(esp_random()) << 32) | esp_random());
  runtime_.hud.setTransport(this);
  if(!runtime_.recovery && (runtime_.modbus.pc||runtime_.modbus.bus)) {
    modbus_.reset(new(std::nothrow) ModbusLink(runtime_));
    if(modbus_)modbus_->link.registers().begin(uint32_t(runtime_.hud.boot));
    else runtime_.hud.catalogError=true;
  }
  if (runtime_.hud.config.uart && runtime_.controller.hardware.hudRs485() && !runtime_.modbus.bus) {
    if (heapFreeBytes() >= sizeof(Rs485Link)+MemoryGuard::CriticalFree &&
        heapMaxBlockBytes() >= sizeof(Rs485Link)+MemoryGuard::CriticalBlock)
      hud485_.reset(new (std::nothrow) Rs485Link);
    if (hud485_) hud485_->link.begin(runtime_.hud.isHud(),runtime_.hud.boot,millis());
    else runtime_.hud.catalogError=true;
  } else if (runtime_.hud.config.uart && !runtime_.modbus.bus) {
    hudUart_.reset(new (std::nothrow) UartLink);
    if (hudUart_) hudUart_->tx.setPaced(runtime_.hud.config.link.paced());
    if (!hudUart_)
      runtime_.hud.catalogError = true;
  }
  if (runtime_.hud.config.uart && !runtime_.controller.hardware.hudRs485() && (runtime_.controller.hardware.pins.uartRx < 0 || runtime_.controller.hardware.pins.uartTx < 0)) {
    runtime_.hud.config.uart = false;
    runtime_.hud.catalogError = true;
  }
}
bool Network::sendHud(const HudEndpoint &e, const uint8_t *bytes, size_t size) {
  int error = 0;
  const bool sent =
      e.uart ? runtime_.hud.config.uart && (modbus_ && runtime_.modbus.bus ? modbus_->link.enqueue(uint8_t(e.address),bytes,size,millis()) : hud485_ ? hud485_->link.enqueue(bytes,size,millis()) : hudUart_ && hudUart_->tx.enqueue(bytes, size))
             : hudUdp_.send(e.address, e.port, {reinterpret_cast<const char *>(bytes), size}, &error);
  if (sent) {
    ++runtime_.hud.sent;
  } else {
    ++runtime_.hud.sendErrors;
    runtime_.hud.sendErrno = error;
  }
  return sent;
}

bool Network::deferReply(const Packet &p, uint8_t port) {
  if (p.result != Result::Deferred)
    return false;
  // GS-232 motion commands have no success reply. On a half-duplex PC link,
  // a delayed error could collide with the PC's next request; it stays in HUD
  // status instead. Queries still receive their immediate position response.
  if (port == 0 && runtime_.controller.hardware.rs485()) return true;
  for (auto &reply : deferred_)
    if (!reply.used) {
      reply = {runtime_.hud.requestId(),
               port == 3   ? tcpSession_
               : port == 1 ? bluetooth_.sessionId()
                           : 0,
               port, p.reply.rfind("RPRT ", 0) == 0, true};
      return true;
    }
  if (port == 3)
    rotctldSend("RPRT -1\n");
  else
    sendReply("?>\r\n");
  return true;
}
void Network::resetHudInputs() {
  if (inputEpoch_ == runtime_.hud.selectionEpoch)
    return;
  inputEpoch_ = runtime_.hud.selectionEpoch;
  if(modbus_ && modbus_->busReady)modbus_->link.cancel();
  bluetooth_.discardInput();
  int queued = 0;
  if (rotctldClient_ && ::ioctl(rotctldClient_.fd(), FIONREAD, &queued) < 0)
    rotctldClient_.stop(); // A failed socket cannot provide a safe input boundary.
  tcpReceiver_.select(queued > 0 ? size_t(queued) : 0);
  // Keep the TCP session identity: replies to accepted commands still belong
  // to this logger, including commands acknowledged after the selection changed.
  setupLoggerUdp(); // Closing the sockets discards every buffered old-selection datagram.
  for (auto &peer : reportPeers)
    peer.used = false;
}
void Network::serviceHud() {
  resetHudInputs();
  auto &hud = runtime_.hud;
  const uint32_t started = millis();
  for (unsigned i = 0; i < 4 && uint32_t(millis() - started) < 2; ++i) {
    uint32_t address = 0;
    uint16_t port = 0;
    const int n = hudUdp_.receiveInto(hudRx_, sizeof hudRx_, address, port);
    if (n == -1)
      break;
    if (n > 0)
      hud.receive({address, port, false}, hudRx_, size_t(n), millis());
  }
  if (hud.config.uart && hudUart_) {
    for (unsigned i = 0; i < 256 && Serial0.available() && uint32_t(millis() - started) < 2; ++i) {
      const auto n = hudUart_->decoder.feed(uint8_t(Serial0.read()), hudRx_, sizeof hudRx_);
      if (n)
        hud.receive({0, hud.config.port, true}, hudRx_, n, millis());
    }
    const int available = Serial0.availableForWrite();
    const auto &pins=runtime_.controller.hardware.pins;
    hudUart_->gate.service(millis(),hud.config.link,pins.hudReady>=0,
        pins.hudReady<0 || bool(digitalRead(pins.hudReady))==hud.config.link.readyHigh,
        available>0?size_t(available):0,hudUart_->tx,
        [](const uint8_t *data,size_t n) { return Serial0.write(data,n); });
  }
  if (hud.config.uart && hud485_) {
    auto &serial=hud485_->serial;
    for (unsigned i=0;i<512 && serial.available() && uint32_t(millis()-started)<3;++i)
      hud485_->link.feed(uint8_t(serial.read()),millis(),[&](const uint8_t *p,size_t n) {
        hud.receive({0,hud.config.port,true},p,n,millis());
      });
    const int available=serial.availableForWrite();
    hud485_->link.service(millis(),available>0?size_t(available):0,
        [&](const uint8_t *p,size_t n) { return serial.write(p,n); },
        [] { return uart_wait_tx_done(UART_NUM_1,0)==ESP_OK; });
  }
  serviceModbusBus();
  hud.service(millis());
  for (auto &reply : deferred_)
    if (reply.used) {
      const auto r = hud.result(reply.id);
      if (r.result == Result::Deferred)
        continue;
      reply.used = false;
      if (reply.port == 3 && reply.session != tcpSession_)
        continue;
      if (reply.port == 1 && reply.session != bluetooth_.sessionId())
        continue;
      const bool success = r.result == Result::Ok || r.result == Result::Pending;
      const char *text = reply.ham ? (success                       ? "RPRT 0\n"
                                      : r.result == Result::Ignored ? "RPRT -8\n"
                                                                    : "RPRT -1\n")
                                   : (success || r.result == Result::Ignored ? "" : "?>\r\n");
      if (!*text)
        continue;
      if (reply.port == 3)
        rotctldSend(text);
      else {
        const auto saved = gsReplyPort;
        gsReplyPort = GsReplyPort(reply.port);
        sendReply(text);
        gsReplyPort = saved;
      }
    }
}
} // namespace forkrot
