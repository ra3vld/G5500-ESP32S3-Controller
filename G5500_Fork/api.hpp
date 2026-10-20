// G5500 Fork; original by UA1CFM, modifications by RA3VLD. GPL-3.0.
#pragma once
#include "protocol.hpp"
#include "json_writer.hpp"
#include <map>
namespace forkrot {
struct Settings {
  unsigned n1mmPort = 12040, pstPort = 1234, reportPort = 13010, commandPort = 4535;
  std::string reportHost;
  bool encoderInverted = true;
};
inline bool commandPortAllowed(unsigned port) {
  // ArduinoOTA, mDNS and (when enabled) the live HTTPD control socket.
  return port && port<=65535 && port!=3232 && port!=5353 &&
      (!WEB_INTERFACE_ENABLED || port!=32769);
}
struct Response {
  int status = 200;
  std::string body;
};
using Form = std::map<std::string, std::string>;
using FormArgument = std::pair<std::string_view, std::string_view>;
struct FormView {
  const FormArgument *entries;
  size_t count;
  const FormArgument *begin() const { return entries; }
  const FormArgument *end() const { return entries + count; }
};
Response apiView(Controller &c, Protocol &p, Settings &settings, std::string_view path,
                 FormView form, uint32_t now);
std::string jsonQuote(const std::string &s);
std::string stateJson(const Controller &c, const Protocol &p, const Settings &settings,
                      uint32_t now);
bool writeStateJson(JsonWriter &json, const Controller &c, const Protocol &p,
                    const Settings &settings, uint32_t now, bool appendMembers = false);
Response api(Controller &c, Protocol &p, Settings &settings, const std::string &path,
             const Form &form, uint32_t now);
Form decodeForm(const std::string &input);
} // namespace forkrot
