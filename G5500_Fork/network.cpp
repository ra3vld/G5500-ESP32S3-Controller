// G5500 Fork; original by UA1CFM, modifications by RA3VLD. GPL-3.0.
#include "network.hpp"
namespace forkrot {
using namespace board;
void Network::sendReply(const String &s) {
  if (gsReplyPort == GS_REPLY_HC05) {
#if BLUETOOTH_ENABLED
    hc05Serial_.print(s);
#endif
    diagnostics_.dbgPrint("[HC05 TX] ");
    diagnostics_.dbgPrintln(s);
  } else {
    Serial0.print(s);
    diagnostics_.dbgPstTx(s);
  }
  hardware_.flashTx();
}

void Network::processGs232Command(String cmd) {
  actions_.updatePosition();
  auto packet = runtime_.protocol.line(cmd.c_str(), millis());
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
}

void Network::handlePstRotatorSerial() {
  static forkrot::LineBuffer receiver;
  std::string command;
  while (Serial0.available()) {
    if (receiver.feed((char)Serial0.read(), command)) {
      gsReplyPort = GS_REPLY_USB;
      String cmd(command.c_str());
      diagnostics_.dbgPstRx(cmd);
      processGs232Command(cmd);
    }
  }
}

bool Network::hc05Connected() {
#if BLUETOOTH_ENABLED
  return digitalRead(PIN_HC05_STATE) == HIGH;

#else
  return false;
#endif
}

void Network::handleHc05Serial() {
#if BLUETOOTH_ENABLED
  static forkrot::LineBuffer receiver;
  std::string command;
  while (hc05Serial_.available()) {
    if (receiver.feed((char)hc05Serial_.read(), command)) {
      gsReplyPort = GS_REPLY_HC05;
      String cmd(command.c_str());
      diagnostics_.dbgPrint("[HC05 RX] ");
      diagnostics_.dbgPrintln(cmd);
      processGs232Command(cmd);
    }
  }

#else
#endif
}

void Network::rotctldSend(const String &s) {
  bool sent = false;
  if (rotctldClient_ && rotctldClient_.connected()) {
    rotctldClient_.print(s);
    hardware_.flashTx();
    sent = true;
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

void Network::processRotctldCommand(String cmd) {
  actions_.updatePosition();
  auto packet = runtime_.protocol.line(cmd.c_str(), millis(), true);
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
    delay(5);
    rotctldClient_.stop();
    return;
  }

  // Minimal info support.
  if (cmd == "_" || lower == "\\get_info" || lower == "get_info") {
    rotctldSend("G5500 ESP32-S3 v1.6.23 UA1CFM Look4Sat bridge\n");
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

  std::string command;
  while (rotctldClient_.connected() && rotctldClient_.available()) {
    if (receiver.feed((char)rotctldClient_.read(), command)) {
      processRotctldCommand(String(command.c_str()));
    }
  }
}

void Network::startWifi() {
  // SAFE Wi-Fi logic:
  // 1) Start the controller AP.
  // 2) If saved home Wi-Fi credentials exist, try STA only ONCE.
  // 3) If home Wi-Fi is unavailable, stop STA completely and return to AP-only.
  // This prevents repeated STA scans from making the SoftAP disappear.

  WiFi.persistent(false);
  WiFi.setSleep(false);

  // Start with AP only.
  WiFi.mode(WIFI_AP);
  delay(250);

  IPAddress apIP(192, 168, 4, 1);
  IPAddress apGW(192, 168, 4, 1);
  IPAddress apMask(255, 255, 255, 0);
  WiFi.softAPConfig(apIP, apGW, apMask);

  bool apOk = credentials_.apPass.length() >= 8 ? WiFi.softAP(AP_SSID, credentials_.apPass.c_str())
                                                : WiFi.softAP(AP_SSID);
  delay(300);

#if FORK_NATIVE_USB
  diagnostics_.dbgPrintln("[V1559] AP-only start");
  diagnostics_.dbgPrint("[V1559] AP start = ");
  diagnostics_.dbgPrintln(apOk ? "OK" : "FAILED");
  diagnostics_.dbgPrint("[V1559] AP IP = ");
  diagnostics_.dbgPrintln(WiFi.softAPIP().toString());
#endif

  // Try home Wi-Fi once, only if credentials are stored.
  if (credentials_.wifiSsid.length()) {
#if FORK_NATIVE_USB
    diagnostics_.dbgPrint("[V1559] one STA attempt: ");
    diagnostics_.dbgPrintln(credentials_.wifiSsid);
#endif

    WiFi.mode(WIFI_AP_STA);
    delay(100);
    WiFi.begin(credentials_.wifiSsid.c_str(), credentials_.wifiPass.c_str());

    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 8000) {
      delay(100);
    }

    if (WiFi.status() == WL_CONNECTED) {
#if FORK_NATIVE_USB
      diagnostics_.dbgPrint("[V1559] LAN connected = ");
      diagnostics_.dbgPrintln(WiFi.localIP().toString());
      diagnostics_.dbgPrint("[V1559] AP remains = ");
      diagnostics_.dbgPrintln(WiFi.softAPIP().toString());
#endif
    } else {
      // Home network not available: completely stop STA and restore AP-only.
      WiFi.disconnect(true, false);
      delay(150);
      WiFi.mode(WIFI_AP);
      delay(150);

      WiFi.softAPConfig(apIP, apGW, apMask);
      if (credentials_.apPass.length() >= 8) {
        WiFi.softAP(AP_SSID, credentials_.apPass.c_str());
      } else {
        WiFi.softAP(AP_SSID);
      }
      delay(250);

#if FORK_NATIVE_USB
      diagnostics_.dbgPrintln("[V1559] LAN unavailable -> AP ONLY");
      diagnostics_.dbgPrint("[V1559] AP restored = ");
      diagnostics_.dbgPrintln(WiFi.softAPIP().toString());
#endif
    }
  } else {
#if FORK_NATIVE_USB
    diagnostics_.dbgPrintln("[V1559] no saved LAN credentials -> AP ONLY");
#endif
  }

  tcpServer_.begin();
  actions_.setupWeb();

#if FORK_NATIVE_USB
  diagnostics_.dbgPrintln(
      "[WIFI] safe auto-reconnect: 30 s interval, 8 s attempt, AP-only fallback");
#endif
}

void Network::restoreApOnlyAfterReconnectFail() {
  // Stop the failed STA side completely and return to the same stable
  // AP-only state used by startWifi(). No long delay is used here because
  // rotator control must continue to run.
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_AP);

  IPAddress apIP(192, 168, 4, 1);
  IPAddress apGW(192, 168, 4, 1);
  IPAddress apMask(255, 255, 255, 0);
  WiFi.softAPConfig(apIP, apGW, apMask);

  if (credentials_.apPass.length() >= 8) {
    WiFi.softAP(AP_SSID, credentials_.apPass.c_str());
  } else {
    WiFi.softAP(AP_SSID);
  }
}

void Network::serviceWifiReconnect() {
  if (!credentials_.wifiSsid.length()) {
    return;
  }

  uint32_t now = millis();

  // LAN came back: remain in AP+STA and arm the timer for a future loss.
  if (WiFi.status() == WL_CONNECTED) {
    if (wifiReconnectTryActive) {
#if FORK_NATIVE_USB
      diagnostics_.dbgPrint("[WIFI] reconnect OK, LAN IP = ");
      diagnostics_.dbgPrintln(WiFi.localIP().toString());
#endif
    }
    wifiReconnectTryActive = false;
    wifiReconnectMs = now;
    return;
  }

  // A reconnect attempt is running. Give it at most 8 seconds.
  // This is non-blocking: the normal control loop continues to execute.
  if (wifiReconnectTryActive) {
    if ((uint32_t)(now - wifiReconnectTryStartedMs) < WIFI_RECONNECT_TIMEOUT_MS) {
      return;
    }

#if FORK_NATIVE_USB
    diagnostics_.dbgPrintln("[WIFI] reconnect timeout -> restore AP ONLY");
#endif

    wifiReconnectTryActive = false;
    wifiReconnectMs = now;
    restoreApOnlyAfterReconnectFail();
    return;
  }

  // First observation after boot/loss: start the 30-second retry interval.
  if (wifiReconnectMs == 0) {
    wifiReconnectMs = now;
    return;
  }

  if ((uint32_t)(now - wifiReconnectMs) < WIFI_RECONNECT_MS) {
    return;
  }

  // One controlled STA attempt. AP remains available while the attempt runs.
#if FORK_NATIVE_USB
  diagnostics_.dbgPrint("[WIFI] reconnect attempt: ");
  diagnostics_.dbgPrintln(credentials_.wifiSsid);
#endif

  if (WiFi.getMode() != WIFI_AP_STA) {
    WiFi.mode(WIFI_AP_STA);
  }

  wifiReconnectTryActive = true;
  wifiReconnectTryStartedMs = now;
  WiFi.begin(credentials_.wifiSsid.c_str(), credentials_.wifiPass.c_str());
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
  for (WiFiUDP *socket : {&n1mmUdp, &pstUdp}) {
    for (unsigned count = 0; count < 4; ++count) {
      int size = socket->parsePacket();
      if (size <= 0) {
        break;
      }
      IPAddress sender = socket->remoteIP();
      if (size > 4096) {
        while (socket->available()) {
          socket->read();
        }
        continue;
      }
      std::string text(size, '\0');
      int got = socket->read(reinterpret_cast<uint8_t *>(&text[0]), size);
      text.resize(got > 0 ? got : 0);
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
      if (packet.matched && text.find("N1MMRotor") != std::string::npos) {
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
        socket->beginPacket(sender, runtime_.loggerSettings.pstPort + 1);
        socket->write(reinterpret_cast<const uint8_t *>(packet.reply.data()), packet.reply.size());
        socket->endPacket();
        hardware_.flashTx();
      }
    }
  }
  if (uint32_t(millis() - lastReportMs) >= 500) {
    lastReportMs = millis();
    String message = runtime_.protocol.positionReport(runtime_.currentAz).c_str();
    for (auto peer : reportPeers) {
      if (peer.used) {
        n1mmUdp.beginPacket(peer.address, runtime_.loggerSettings.reportPort);
        n1mmUdp.print(message);
        n1mmUdp.endPacket();
      }
    }
    IPAddress host;
    if (!runtime_.loggerSettings.reportHost.empty() &&
        host.fromString(runtime_.loggerSettings.reportHost.c_str())) {
      n1mmUdp.beginPacket(host, runtime_.loggerSettings.reportPort);
      n1mmUdp.print(message);
      n1mmUdp.endPacket();
    }
  }
}
void Network::beginSerial() {
  Serial0.begin(9600, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);
#if BLUETOOTH_ENABLED
  pinMode(PIN_HC05_STATE, INPUT);
  hc05Serial_.begin(HC05_BAUD, SERIAL_8N1, PIN_HC05_RX, PIN_HC05_TX);
#endif
}

} // namespace forkrot
