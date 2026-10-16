// G5500 Fork; original by UA1CFM. GPL-3.0.
#include "network.hpp"
#include "wifi_memory.hpp"
#include "tcp_memory.hpp"
#include "runtime_metrics.hpp"
#include "crowpanel_board.hpp"
#include <esp_app_desc.h>
#include <cstdio>
#include "write_budget.hpp"
#include <cerrno>
namespace forkrot {
using namespace board;
void Network::serviceHeapDiagnostics() {
  heapDiagnostics_.service(millis(),
      [this](uint8_t page, char *reply, size_t capacity) {
        return formatHeapDiagnostics(page, reply, capacity);
      }, [this](DiagnosticStream::Port port, const char *data, size_t size) -> size_t {
        if (port == DiagnosticStream::Port::Bluetooth)
          return bluetooth_.send(std::string_view(data, size)) ? size : 0;
#if SELECTED_BOARD == BOARD_CROWPANEL_21 && ARDUINO_USB_CDC_ON_BOOT
        if (port == DiagnosticStream::Port::NativeUsb) {
          const int available = Serial.availableForWrite();
          return available > 0 ? Serial.write(reinterpret_cast<const uint8_t *>(data),
              std::min(size, size_t(available))) : 0;
        }
#endif
        const int available = Serial0.availableForWrite();
        if (available <= 0) return 0;
        const size_t count = size_t(available) < size ? size_t(available) : size;
        return Serial0.write(reinterpret_cast<const uint8_t *>(data), count);
      });
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
        bluetooth_.linkConnected() ? "yes" : "no", compactWifi_ ? "compact" : "standard");
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
    return std::snprintf(reply, capacity, "TCP stats=%s active=%u closing=%u timewait=%u pcbBytes=%u\r\n",
        tcp.available ? "OK" : "unavailable", unsigned(tcp.active), unsigned(tcp.closing),
        unsigned(tcp.timewait), unsigned(tcp.pcbBytes));
  }
  case 5: {
    const auto mode = WiFi.getMode();
    return std::snprintf(reply, capacity, "WIFI runtimeMode=%s apClients=%u apPolicy=%s\r\n",
        mode == WIFI_STA ? "STA" : mode == WIFI_AP ? "AP" : mode == WIFI_AP_STA ? "AP+STA" : "OFF",
        (mode & WIFI_AP) ? unsigned(WiFi.softAPgetStationNum()) : 0,
        WIFI_AP_ALWAYS_ON ? "always" : "fallback");
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
#if SELECTED_BOARD == BOARD_CROWPANEL_21 && ARDUINO_USB_CDC_ON_BOOT
  if (gsReplyPort == GS_REPLY_NATIVE_USB) {
    Serial.print(s);
    hardware_.flashTx();
    return;
  }
#endif
  if (gsReplyPort == GS_REPLY_BLUETOOTH) {
    if (!bluetooth_.send(s)) diagnostics_.dbgPrintln("[BT TX] dropped: disconnected or queue full");
    diagnostics_.dbgPrint("[BT TX] ");
    diagnostics_.dbgPrintln(s);
  } else {
    Serial0.print(s);
    diagnostics_.dbgPstTx(s);
  }
  hardware_.flashTx();
}

void Network::processGs232Command(std::string_view input) {
  actions_.updatePosition();
  auto packet = runtime_.protocol.line(input, millis());
  if (packet.matched) {
    if (packet.result == forkrot::Result::Ok || packet.result == forkrot::Result::Pending) {
      runtime_.targetValid = true;
      runtime_.lastTargetRxMs = millis();
    }
    runtime_.controller.tick(millis());
    hardware_.applyOutputs();
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
  if (cmd == "P36") {
    actions_.setGsAzMode(36);
    return;
  }
  if (cmd == "P45") {
    actions_.setGsAzMode(45);
    return;
  }

  // Standard GS-232 calibration commands.
  // O  = current AZ feedback becomes 0 degrees
  // F  = current AZ feedback becomes full scale (360 or 450, from P36/P45)
  // O2 = current EL feedback becomes 0 degrees
  // F2 = current EL feedback becomes 180 degrees
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
#if CROWPANEL_SIMULATION
  if (gsReplyPort == GS_REPLY_NATIVE_USB && cmd.startsWith("SIM ")) {
    float az = 0, el = 0; char extra;
    const auto result = std::sscanf(cmd.c_str(), "SIM %f %f %c", &az, &el, &extra) == 2
        ? runtime_.controller.target({az, el, true, true}, false, millis()) : Result::Invalid;
    hardware_.controlRotator();
    sendReply(String("SIM ") + Controller::resultName(result) + "\r\n");
    return;
  }
#endif
  if (cmd == "H5") {
    const auto stats = crowpanel::displayStats();
    char reply[256];
    std::snprintf(reply, sizeof reply,
        "CROW ms=%lu az=%.3f el=%.3f targetAz=%.3f targetEl=%.3f moving=%d sim=%d inv=%d vsync=%lu frames=%lu waitMaxUs=%lu timeouts=%lu failed=%d\r\n",
        (unsigned long)millis(), double(runtime_.controller.axes[0].current), double(runtime_.controller.axes[1].current),
        double(runtime_.controller.axes[0].target), double(runtime_.controller.axes[1].target),
        int(runtime_.controller.moving()), CROWPANEL_SIMULATION, int(runtime_.loggerSettings.encoderInverted), (unsigned long)stats.vsyncs,
        (unsigned long)stats.frames, (unsigned long)stats.waitMaxUs, (unsigned long)stats.timeouts, int(stats.failed));
    sendReply(reply);
    return;
  }
#endif
}

void Network::handlePstRotatorSerial() {
#if SELECTED_BOARD == BOARD_CROWPANEL_21 && ARDUINO_USB_CDC_ON_BOOT
  // Native USB allows memory/display diagnostics and the same simulated GS-232
  // commands without changing the host's Wi-Fi connection to the panel's AP.
  static forkrot::LineBuffer usbReceiver;
  std::string_view usbCommand;
  const uint32_t usbStarted = millis();
  unsigned usbCommands = 0;
  for (unsigned bytes = 0; bytes < 128 && usbCommands < 4 && Serial.available() && uint32_t(millis() - usbStarted) < 2; ++bytes) {
    if (usbReceiver.feed(char(Serial.read()), usbCommand)) {
      ++usbCommands;
      gsReplyPort = GS_REPLY_NATIVE_USB;
      processGs232Command(usbCommand);
    }
  }
#endif
  if (PIN_UART_RX < 0 || PIN_UART_TX < 0) return;
  static forkrot::LineBuffer receiver;
  std::string_view command;
  const uint32_t started = millis();
  unsigned commands = 0;
  for (unsigned bytes = 0; bytes < 128 && commands < 4 && Serial0.available() && uint32_t(millis() - started) < 2; ++bytes) {
    if (receiver.feed((char)Serial0.read(), command)) {
      ++commands;
      gsReplyPort = GS_REPLY_USB;
      diagnostics_.dbgView("[PST RX] "); diagnostics_.dbgView(command, true);
      processGs232Command(command);
    }
  }
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
  actions_.updatePosition();
  auto packet = runtime_.protocol.line(input, millis(), true);
  if (packet.matched) {
    if (packet.result == forkrot::Result::Ok || packet.result == forkrot::Result::Pending) {
      runtime_.targetValid = true;
      runtime_.lastTargetRxMs = millis();
    }
    runtime_.controller.tick(millis());
    hardware_.applyOutputs();
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
    s += (calibration_.state().gsAzMode == 45 ? "450" : "360");
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
  static forkrot::LineBuffer receiver;
  // Accept a client if none is currently connected.
  if (!rotctldClient_ || !rotctldClient_.connected()) {
    WiFiClient c = tcpServer_.accept();
    if (c) {
      if (rotctldClient_) {
        rotctldClient_.stop();
      }
      rotctldClient_ = c;
      receiver.clear();
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
    if (receiver.feed(byte, command)) { ++commands; processRotctldCommand(command); }
  }
}

void Network::startWifi() {
  WiFi.persistent(false);
#if BLUETOOTH_BACKEND == BT_ESP32_SPP
  const esp_err_t initResult = initCompactWifi();
  compactWifi_ = initResult == ESP_OK;
  diagnostics_.dbgPrintln(compactWifi_ ? "[WIFI] compact buffers: static RX 2, dynamic RX/TX 4/2, aggregation off"
                                      : "[WIFI] compact init failed; trying Arduino defaults");
#endif
  WiFi.setSleep(false);
  // The policy below owns retries; Arduino's event callback must not launch
  // an independent reconnect after we deliberately return to AP-only mode.
  WiFi.setAutoReconnect(false);
#if SELECTED_BOARD == BOARD_ESP32 && ESP32_FORCE_AP_STA
  diagnostics_.dbgPrintln("[WIFI] WARNING: forced AP+STA uses more RAM; may be unstable, especially with SPP");
#endif
  if (WIFI_AP_ALWAYS_ON) restoreApOnlyAfterReconnectFail();
  if (credentials_.wifiSsid.length()) {
    diagnostics_.dbgPrintln(WIFI_AP_ALWAYS_ON ? "[WIFI] starting STA; AP remains available"
                                            : "[WIFI] starting STA; AP reserved for fallback");
    if (WiFi.mode(WIFI_AP_ALWAYS_ON ? WIFI_AP_STA : WIFI_STA)) {
      WiFi.begin(credentials_.wifiSsid.c_str(), credentials_.wifiPass.c_str());
      const uint32_t started = millis();
      while (WiFi.status() != WL_CONNECTED && uint32_t(millis() - started) < WifiFallback::AttemptMs)
        delay(100);
    }
  }
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected) {
    diagnostics_.dbgPrint(WIFI_AP_ALWAYS_ON ? "[WIFI] STA connected, AP remains, IP = "
                                          : "[WIFI] STA connected, AP off, IP = ");
    diagnostics_.dbgPrintln(WiFi.localIP().toString());
  } else if (!WIFI_AP_ALWAYS_ON || WiFi.getMode() != WIFI_AP) {
    restoreApOnlyAfterReconnectFail();
  }
  wifiFallback_.reset(millis(), connected);
  tcpServer_.begin();
  actions_.setupWeb();
  heapAfterWifi_ = heapFreeBytes();
  diagnostics_.dbgPrintln(WIFI_AP_ALWAYS_ON ? "[WIFI] AP always on; STA retry 30s, attempt 8s"
                                          : "[WIFI] STA preferred; AP fallback, retry 30s, attempt 8s");
}

void Network::restoreApOnlyAfterReconnectFail() {
  // No driver deinit/reinit: keep the compact buffer configuration. The mode
  // transition stops the failed STA and releases its connection state.
  if (WiFi.getMode() & WIFI_STA) WiFi.disconnect(false, false);
  if (!WiFi.mode(WIFI_AP)) {
    diagnostics_.dbgPrintln("[WIFI] AP mode failed");
    return;
  }
  const IPAddress apIP(192, 168, 4, 1), apMask(255, 255, 255, 0);
  const bool configured = WiFi.softAPConfig(apIP, apIP, apMask);
  const bool started = credentials_.apPass.length() >= 8
      ? WiFi.softAP(AP_SSID, credentials_.apPass.c_str()) : WiFi.softAP(AP_SSID);
  heapAfterAp_ = heapFreeBytes(); // zero means AP has not been started this boot
  diagnostics_.dbgPrintln(configured && started ? "[WIFI] AP ready at 192.168.4.1"
                                               : "[WIFI] AP start failed");
}

void Network::serviceWifiReconnect() {
  const auto action = wifiFallback_.update(millis(), credentials_.wifiSsid.length() != 0,
      WiFi.status() == WL_CONNECTED, (WiFi.getMode() & WIFI_AP) != 0, WIFI_AP_ALWAYS_ON);
  if (action == WifiAction::StopAp) {
    if (WiFi.mode(WIFI_STA)) diagnostics_.dbgPrintln("[WIFI] STA connected; fallback AP stopped");
    else diagnostics_.dbgPrintln("[WIFI] failed to stop fallback AP");
  } else if (action == WifiAction::StartAp) {
    restoreApOnlyAfterReconnectFail();
  } else if (action == WifiAction::StartSta) {
    diagnostics_.dbgPrintln("[WIFI] STA reconnect attempt; AP remains during attempt");
    if (WiFi.mode(WIFI_AP_STA))
      WiFi.begin(credentials_.wifiSsid.c_str(), credentials_.wifiPass.c_str());
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
  if (uint32_t(millis() - lastReportMs) >= 500) {
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
void Network::beginSerial() {
  if (PIN_UART_RX >= 0 && PIN_UART_TX >= 0) Serial0.begin(9600, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);
  heapBeforeBt_ = heapFreeBytes();
  btReady_ = bluetooth_.begin();
  if (!btReady_) diagnostics_.dbgPrintln("[BT] initialization failed; USB and network remain available");
  heapAfterBt_ = heapFreeBytes();
}

} // namespace forkrot
