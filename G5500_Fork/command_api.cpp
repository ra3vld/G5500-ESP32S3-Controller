// G5500 Fork; GPL-3.0.
#include "command_api.hpp"
#include <cstdio>
#include <cstring>
namespace forkrot {
namespace {
int hex(unsigned char c) {
  return c >= '0' && c <= '9' ? c-'0' : c >= 'a' && c <= 'f' ? c-'a'+10 : c >= 'A' && c <= 'F' ? c-'A'+10 : -1;
}
bool integer(std::string_view s, uint32_t &out) {
  if (s.empty() || s.size() > 10) return false;
  out = 0;
  for (char c : s) {
    if (c < '0' || c > '9' || out > (UINT32_MAX - unsigned(c-'0')) / 10) return false;
    out = out * 10 + unsigned(c-'0');
  }
  return true;
}
uint32_t fingerprint(std::string_view s) {
  uint32_t value = 2166136261u;
  for (unsigned char c : s) value = (value ^ c) * 16777619u;
  return value;
}
void base64(JsonWriter &j, std::string_view s) {
  static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  for (size_t i = 0; i < s.size(); i += 3) {
    const uint32_t n = (uint32_t(uint8_t(s[i])) << 16) |
        (i+1 < s.size() ? uint32_t(uint8_t(s[i+1])) << 8 : 0) |
        (i+2 < s.size() ? uint8_t(s[i+2]) : 0);
    j << alphabet[n >> 18] << alphabet[(n >> 12) & 63]
      << (i+1 < s.size() ? alphabet[(n >> 6) & 63] : '=')
      << (i+2 < s.size() ? alphabet[n & 63] : '=');
  }
}
}
bool CommandForm::parse(std::string_view text,size_t limit) {
  count = 0;
  if (limit>=sizeof bytes || text.size() > limit) return false;
  size_t used = 0;
  while (!text.empty()) {
    if (count == entries.size()) return false;
    const auto amp = text.find('&');
    const auto pair = text.substr(0, amp);
    const auto equal = pair.find('=');
    if (equal == pair.npos || !equal) return false;
    std::string_view values[2];
    for (unsigned part = 0; part < 2; ++part) {
      const auto raw = part ? pair.substr(equal+1) : pair.substr(0,equal);
      const size_t start = used;
      for (size_t i = 0; i < raw.size(); ++i) {
        unsigned char c = raw[i];
        if (c == '%') {
          if (i+2 >= raw.size() || hex(raw[i+1]) < 0 || hex(raw[i+2]) < 0) return false;
          c = unsigned(hex(raw[i+1])*16 + hex(raw[i+2])); i += 2;
        } else if (c == '+') c = ' ';
        if (c < 32 || c == 127 || used == limit) return false;
        bytes[used++] = char(c);
      }
      values[part] = {bytes+start,used-start};
      if (values[part].size() > (part ? 128u : 32u)) return false;
    }
    for (size_t i = 0; i < count; ++i) if (entries[i].first == values[0]) return false;
    entries[count++] = {values[0],values[1]};
    if (amp == text.npos) break;
    text.remove_prefix(amp+1);
    if (text.empty()) return false;
  }
  return true;
}
std::string_view CommandForm::get(std::string_view name) const {
  for (size_t i = 0; i < count; ++i) if (entries[i].first == name) return entries[i].second;
  return {};
}
void CommandServer::begin(uint64_t boot, uint32_t seed, Dispatch dispatch, void *context, const uint32_t *selection) {
  boot_ = boot; nextSession_ = seed; dispatch_ = dispatch; context_ = context;
  selection_ = selection;
  for (auto &s : sessions_) s.sid = 0;
}
size_t CommandServer::process(uint64_t peer, std::string_view request, uint32_t now, char *reply, size_t capacity) {
  if (!capacity) return 0;
  capacity = std::min(capacity, COMMAND_REPLY_MAX + 1);
  auto &form=request_;
  uint32_t cid = 0, sid = 0, offset = 0;
  char boot[17]; std::snprintf(boot,sizeof boot,"%016llx",static_cast<unsigned long long>(boot_));
  auto error = [&](int status, const char *reason) {
    JsonWriter j(reply,capacity);
    j << "{\"v\":1,\"cid\":" << cid << ",\"boot\":" << jsonQuoted(boot)
      << ",\"status\":" << status << ",\"error\":" << jsonQuoted(reason) << '}';
    return j.good() ? j.size() : size_t(0);
  };
  if (request.empty() || request.front() != '@' || request.size() > COMMAND_INPUT_MAX ||
      !form.parse(request.substr(1)) || form.get("v") != "1" ||
      !integer(form.get("cid"),cid) || !cid) return error(400,"invalid_request");
  const auto method = form.get("method"), path = form.get("path");
  if ((method != "GET" && method != "POST") || path.substr(0,5) != "/api/") return error(400,"invalid_operation");
  if (method == "GET" && path == "/api/hello") {
    uint32_t client = 0;
    if (!integer(form.get("client"),client) || !client) return error(400,"client_required");
    Session *session = nullptr;
    for (auto &s : sessions_) if (s.sid && s.peer == peer && s.client == client) session = &s;
    if (!session) {
      session = &sessions_[0];
      for (auto &s : sessions_) if (!s.sid || uint32_t(now-s.touched) > uint32_t(now-session->touched)) session = &s;
      session->peer = peer; session->client = client; session->cid = 0; session->size = 0;
      if (!++nextSession_) ++nextSession_;
      session->sid = nextSession_;
    }
    session->touched = now;
    JsonWriter j(reply,capacity);
    j << "{\"v\":1,\"cid\":" << cid << ",\"boot\":" << jsonQuoted(boot)
      << ",\"sid\":" << session->sid << ",\"selection\":" << (selection_?*selection_:0)
      << ",\"status\":200,\"inputMax\":1024,\"replyMax\":1200,\"pageMax\":" << COMMAND_PAGE_MAX << '}';
    return j.good() ? j.size() : size_t(0);
  }
  if (form.get("boot") != boot || !integer(form.get("sid"),sid) || !sid) return error(409,"session_required");
  Session *session = nullptr;
  for (auto &s : sessions_) if (s.sid == sid && s.peer == peer) session = &s;
  if (!session) return error(409,"session_required");
  const auto hash = fingerprint(request);
  if (cid == session->cid && hash == session->fingerprint && session->size) {
    if (session->size >= capacity) return error(503,"reply_capacity");
    std::memcpy(reply,session->reply,session->size+1); return session->size;
  }
  if (cid <= session->cid) return error(409,"stale_request");
  const bool selectedOperation=path=="/api/target" || path=="/api/move" || path=="/api/execute" ||
      path=="/api/preset/run" || path=="/api/mode" || path=="/api/motion/reset" || path=="/api/hud/select";
  uint32_t selection=0;
  if(selection_ && selectedOperation && (!integer(form.get("selection"),selection) || selection!=*selection_)) return error(409,"selection_changed");
  if (!form.get("page").empty() && !integer(form.get("page"),offset)) return error(400,"invalid_page");
  if (method == "POST" && offset) return error(400,"invalid_page");
  // Strip protocol metadata: applications see exactly the original API fields.
  // Compact metadata in place; avoid a second form array on the ESP32 loop stack.
  auto &arguments = form.entries;
  size_t count = 0;
  for (auto entry : form.view()) {
    const auto key = entry.first;
    if (key == "v" || key == "cid" || key == "sid" || key == "boot" || key == "method" || key == "path" || key == "page" || key == "selection") continue;
    if (count == arguments.size()) return error(400,"too_many_arguments");
    arguments[count++] = entry;
  }
  // Every request with a nonzero offset must be a read; legacy mutating GETs
  // remain supported by HTTP only. Wire clients use POST for those operations.
  if (method == "GET" && (path == "/api/cal" || path == "/api/cal2" || path == "/api/calmode" ||
      path == "/api/azmode" || path == "/api/calreset" || path == "/api/wifi" || path == "/api/ap" ||
      path == "/api/move" || path == "/api/target" || path.find("/clear") != path.npos)) return error(405,"use_post");
  char data[COMMAND_PAGE_MAX+1];
  JsonWriter body(data,sizeof data); body.page(offset);
  int status = 200; const char *type = "application/json";
  if (!dispatch_) return error(503,"unavailable");
  auto cache = [&](size_t size) {
    session->cid=cid;session->fingerprint=hash;session->touched=now;session->size=size;
    if(size)std::memcpy(session->reply,reply,size+1);
    return size;
  };
  dispatch_(context_,method,path,{arguments.data(),count},body,status,type);
  if (!body.good() || (method == "POST" && body.total() > COMMAND_PAGE_MAX)) return cache(error(503,"response_too_large"));
  JsonWriter j(reply,capacity);
  j << "{\"v\":1,\"cid\":" << cid << ",\"boot\":" << jsonQuoted(boot) << ",\"sid\":" << sid
    << ",\"selection\":" << (selection_?*selection_:0)
    << ",\"status\":" << status << ",\"offset\":" << offset << ",\"next\":";
  if (offset + body.size() < body.total()) j << offset + body.size(); else j << "null";
  j << ",\"total\":" << body.total() << ",\"revision\":" << body.fingerprint()
    << ",\"type\":" << jsonQuoted(type) << ",\"base64\":\"";
  base64(j,{data,body.size()}); j << "\"}";
  if (!j.good()) return cache(error(503,"response_too_large"));
  return cache(j.size());
}
}
