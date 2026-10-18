// HUD configuration and paginated catalogue API. GPL-3.0.
#include "web_server.hpp"
#include <cerrno>
#include <cstdlib>
namespace forkrot {
namespace {
bool nodeId(const String &text, uint64_t &id) {
  if (!text.length() || text.length() > 12)
    return false;
  id = 0;
  for (size_t i = 0; i < text.length(); ++i) {
    const char c = text.c_str()[i];
    int n = c >= '0' && c <= '9'   ? c - '0'
            : c >= 'a' && c <= 'f' ? c - 'a' + 10
            : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                   : -1;
    if (n < 0)
      return false;
    id = (id << 4) | unsigned(n);
  }
  return id != 0;
}
bool integerArg(const String &value, unsigned &out, unsigned maximum = 65535) {
  if (!value.length() || value.length() > 10)
    return false;
  out = 0;
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value.c_str()[i];
    if (c < '0' || c > '9' || out > (maximum - unsigned(c - '0')) / 10)
      return false;
    out = out * 10 + unsigned(c - '0');
  }
  return out <= maximum;
}
} // namespace
void WebInterface::setupHudWeb() {
  web_.on("/api/device", HTTP_GET, [this] {
    JsonWriter j(statusBuffer_, sizeof statusBuffer_);
    j << "{\"ok\":true";
    runtime_.hud.writeStatus(j);
    j << '}';
    sendJson(j);
  });
  web_.on("/api/device", HTTP_POST, [this] {
    auto config = runtime_.hud.config;
    const String role = web_.arg("role"), wifi = web_.arg("wifi"), uart = web_.arg("uart");
    unsigned port = 0;
    if ((role != "hud" && role != "controller") ||
        (wifi != "existing" && wifi != "host" && wifi != "client") || (uart != "0" && uart != "1") ||
        !integerArg(web_.arg("port"), port) ||
        !network_.hudPortValid(port, runtime_.loggerSettings.n1mmPort, runtime_.loggerSettings.pstPort,
                               runtime_.loggerSettings.reportPort)) {
      web_.send(400, "application/json", "{\"ok\":false,\"result\":\"invalid_or_port_conflict\"}");
      return;
    }
    if (uart == "1" && (board::PIN_UART_RX < 0 || board::PIN_UART_TX < 0 ||
        (SELECTED_BOARD == BOARD_CROWPANEL_21 && runtime_.controller.hardware.backend() == BT_HC05))) {
      web_.send(400, "application/json", "{\"ok\":false,\"result\":\"uart_in_use_by_hc05\"}");
      return;
    }
    config.role = role == "hud" ? DeviceRole::Hud : DeviceRole::Controller;
    config.wifi = wifi == "host" ? HudWifi::Host : wifi == "client" ? HudWifi::Client : HudWifi::Existing;
    config.uart = uart == "1";
    config.port = uint16_t(port);
    if (!storage_.saveHudConfig(config)) {
      web_.send(507, "application/json", "{\"ok\":false,\"result\":\"storage_full\"}");
      return;
    }
    // Stop local outputs before rebooting into another role. Do not alter remote numerical jobs.
    if (!runtime_.hud.isHud()) {
      runtime_.controller.stopNow(-1, millis());
      hardware_.applyOutputs();
      runtime_.controller.driveFault = true;
    }
    web_.send(200, "application/json", "{\"ok\":true,\"result\":\"restart\"}");
    restart_.request(millis());
  });
  web_.on("/api/hud/peers", HTTP_GET, [this] {
    unsigned offset = 0, limit = 8;
    if ((web_.hasArg("offset") && !integerArg(web_.arg("offset"), offset, 1000000000)) ||
        (web_.hasArg("limit") && (!integerArg(web_.arg("limit"), limit, 8) || !limit))) {
      web_.send(400, "application/json", "{\"error\":\"invalid_page\"}");
      return;
    }
    JsonWriter j(statusBuffer_, sizeof statusBuffer_);
    runtime_.hud.writePeers(j, offset, limit, web_.arg("discovered") == "1");
    sendJson(j);
  });
  web_.on("/api/hud/discover", HTTP_POST, [this] {
    auto &hud = runtime_.hud;
    if (!hud.isHud()) {
      web_.send(409, "application/json", "{\"ok\":false,\"result\":\"hud_only\"}");
      return;
    }
    HudEndpoint endpoint;
    endpoint.port = hud.config.port;
    endpoint.uart = web_.arg("transport") == "uart";
    if (endpoint.uart && !hud.config.uart) {
      web_.send(400, "application/json", "{\"ok\":false,\"result\":\"uart_disabled\"}");
      return;
    }
    if (!endpoint.uart) {
      IPAddress ip;
      const String address = web_.arg("address");
      if (address.length()) {
        if (!ip.fromString(address) || ip.type() != IPv4 || uint32_t(ip) == 0) {
          web_.send(400, "application/json", "{\"ok\":false,\"result\":\"invalid_address\"}");
          return;
        }
        endpoint.address = uint32_t(ip);
      } else
        endpoint.address = 0xffffffffu;
    }
    unsigned port = endpoint.port;
    if (web_.hasArg("port") && (!integerArg(web_.arg("port"), port) || !port)) {
      web_.send(400, "application/json", "{\"ok\":false,\"result\":\"invalid_port\"}");
      return;
    }
    endpoint.port = uint16_t(port);
    const bool ok = hud.discover(endpoint);
    web_.send(ok ? 200 : 503, "application/json",
              ok ? "{\"ok\":true,\"result\":\"searching\"}" : "{\"ok\":false,\"result\":\"unavailable\"}");
  });
  web_.on("/api/hud/peers", HTTP_POST, [this] {
    auto &hud = runtime_.hud;
    uint64_t id = 0;
    if (!hud.isHud() || !nodeId(web_.arg("id"), id)) {
      web_.send(400, "application/json", "{\"ok\":false,\"result\":\"invalid\"}");
      return;
    }
    auto *p = hud.peer(id);
    if (!p) {
      web_.send(404, "application/json", "{\"ok\":false,\"result\":\"discover_first\"}");
      return;
    }
    const bool oldSaved = p->saved;
    char oldLabel[49];
    std::memcpy(oldLabel, p->label, sizeof oldLabel);
    if (web_.arg("action") == "remove")
      p->saved = false;
    else {
      const auto label = web_.arg("name");
      if (!hud.add(id, p->endpoint, label.length() ? label.c_str() : p->state.name, true)) {
        web_.send(400, "application/json", "{\"ok\":false,\"result\":\"invalid_name\"}");
        return;
      }
    }
    if (!storage_.saveHudPeers(hud)) {
      p->saved = oldSaved;
      std::memcpy(p->label, oldLabel, sizeof oldLabel);
      web_.send(507, "application/json", "{\"ok\":false,\"result\":\"storage_full\"}");
      return;
    }
    bool selectionSaved = true;
    if (!p->saved) {
      hud.remove(id);
      selectionSaved = storage_.saveHudConfig(hud.config);
    } else if (!hud.config.selected) {
      hud.select(id);
      selectionSaved = storage_.saveHudConfig(hud.config);
    }
    if (!selectionSaved) {
      hud.catalogError = true;
      web_.send(507, "application/json", "{\"ok\":false,\"result\":\"storage_full\"}");
      return;
    }
    web_.send(200, "application/json", "{\"ok\":true,\"result\":\"ok\"}");
  });
  web_.on("/api/hud/select", HTTP_POST, [this] {
    auto &hud = runtime_.hud;
    uint64_t id = 0;
    const auto old = hud.config.selected;
    if (!hud.isHud() || !nodeId(web_.arg("id"), id) || !hud.select(id)) {
      web_.send(400, "application/json", "{\"ok\":false,\"result\":\"invalid_peer\"}");
      return;
    }
    if (!storage_.saveHudConfig(hud.config)) {
      hud.select(old);
      web_.send(507, "application/json", "{\"ok\":false,\"result\":\"storage_full\"}");
      return;
    }
    actions_.invalidateDisplay();
    web_.send(200, "application/json", "{\"ok\":true,\"result\":\"ok\"}");
  });
  web_.on("/api/hud/command", HTTP_GET, [this] {
    unsigned request = 0;
    if (!integerArg(web_.arg("id"), request, UINT32_MAX) || !request) {
      web_.send(400, "application/json", "{\"error\":\"invalid_id\"}");
      return;
    }
    const auto reply = runtime_.hud.result(request);
    JsonWriter j(statusBuffer_, sizeof statusBuffer_);
    j << "{\"requestId\":" << request
      << ",\"ok\":" << ((reply.result == Result::Ok || reply.result == Result::Pending) ? "true" : "false")
      << ",\"result\":" << jsonQuoted(Controller::resultName(reply.result)) << '}';
    web_.sendBuffer(reply.result == Result::Deferred ? 202 : 200, "application/json", j.data(), j.size());
  });
}
} // namespace forkrot
