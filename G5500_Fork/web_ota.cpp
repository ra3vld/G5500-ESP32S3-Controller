// G5500 Fork; original by UA1CFM. GPL-3.0.
#include "web_server.hpp"
#include <cstdio>
namespace forkrot {
bool WebInterface::handleOtaRequest(std::string_view request) {
  const auto first = request.find(' '), last = request.find(' ', first + 1);
  const auto method = request.substr(0, first), url = request.substr(first + 1, last - first - 1);
  const auto query = url.find('?');
  const auto path = url.substr(0, query);
  const bool otaPath = path == "/api/ota" || path.substr(0, 9) == "/api/ota/";
  if (!otaPath) {
    if (!ota_.locked() || (method == "GET" && (path == "/" || path == "/api/status" || path == "/diaglog" || path == "/api/diaglog"))) return false;
    web_.send(503, "application/json", "{\"error\":\"update_locked\"}");
    return true;
  }
  web_.sendHeader("Cache-Control", "no-store");
  if (method == "GET" && url == "/api/ota") {
    const auto size = ota_.status(statusBuffer_, sizeof statusBuffer_);
    web_.sendBuffer(200, "application/json", statusBuffer_, size);
    return true;
  }
  // A custom header and a per-boot random token prevent cross-origin form uploads.
  if (method != "POST" || !ota_.authorized(httpHeader(request, "x-ota-token"))) {
    web_.send(403, "application/json", "{\"error\":\"ota_token\"}"); return true;
  }
  const auto parameter = query == url.npos ? std::string_view{} : url.substr(query + 1);
  const auto body = request.substr(request.find("\r\n\r\n") + 4);
  const char *error = "invalid_request";
  uint32_t value = 0;
  if (path == "/api/ota/start" && parameter.substr(0, 5) == "size=" && body.empty() && requestGeneration(parameter.substr(5), value))
    error = ota_.start(value);
  else if (path == "/api/ota/write" && parameter.substr(0, 7) == "offset=" && requestGeneration(parameter.substr(7), value))
    error = ota_.write(value, body);
  else if (url == "/api/ota/finish" && body.empty()) error = ota_.finish();
  else if (url == "/api/ota/cancel" && body.empty()) { ota_.cancel(); error = nullptr; }
  else if (url == "/api/ota/reboot" && body.empty() && ota_.locked()) { ota_.reboot(); error = nullptr; }
  else if (url == "/api/ota/arduino" && body.empty()) error = ota_.armArduino();
  if (error) {
    std::snprintf(statusBuffer_, sizeof statusBuffer_, "{\"error\":\"%s\"}", error);
    web_.sendBuffer(409, "application/json", statusBuffer_, std::strlen(statusBuffer_));
  } else {
    if (ota_.locked() && (path == "/api/ota/start" || path == "/api/ota/write" || path == "/api/ota/finish"))
      web_.holdTransferConnection();
    static constexpr char ok[] = "{\"ok\":true}";
    web_.sendBuffer(200, "application/json", ok, sizeof ok - 1);
  }
  return true;
}
} // namespace forkrot
