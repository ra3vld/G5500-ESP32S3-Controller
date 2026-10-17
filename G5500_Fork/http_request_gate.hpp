// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
namespace forkrot {
enum class RequestState { Incomplete, Ready, Invalid, TooLarge };
inline bool headerEquals(std::string_view text, std::string_view name) {
  if (text.size() != name.size()) return false;
  for (size_t i = 0; i < text.size(); ++i) {
    char ch = text[i];
    if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    if (ch != name[i]) return false;
  }
  return true;
}
// Only the frequent, read-only status response uses persistent HTTP. Other
// routes keep their existing close-after-response behavior. No pipelining.
inline bool persistentStatusRequest(std::string_view request, size_t completeSize) {
  if (request.size() != completeSize) return false;
  const auto end = request.find("\r\n");
  if (end == request.npos || request.substr(0, end) != "GET /api/status HTTP/1.1") return false;
  size_t pos = end + 2;
  while (pos < request.size()) {
    const auto next = request.find("\r\n", pos);
    if (next == request.npos || next == pos) break;
    const auto line = request.substr(pos, next - pos);
    const auto colon = line.find(':');
    if (colon == line.npos) return false;
    if (headerEquals(line.substr(0, colon), "connection")) {
      auto value = line.substr(colon + 1);
      do {
        const auto comma = value.find(',');
        auto token = value.substr(0, comma);
        while (!token.empty() && (token.front() == ' ' || token.front() == '\t')) token.remove_prefix(1);
        while (!token.empty() && (token.back() == ' ' || token.back() == '\t')) token.remove_suffix(1);
        if (headerEquals(token, "close")) return false;
        if (comma == value.npos) break;
        value.remove_prefix(comma + 1);
      } while (!value.empty());
    }
    pos = next + 2;
  }
  return true;
}
inline constexpr size_t HTTP_MAX_ARGUMENTS = 32;
inline bool boundedForm(std::string_view text, size_t &arguments) {
  auto decodedSize = [](std::string_view value, size_t limit) {
    size_t length = 0;
    auto hex = [](unsigned char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    };
    for (size_t i = 0; i < value.size(); ++i) {
      unsigned char c = value[i];
      if (c == '%') {
        if (i + 2 >= value.size()) return false;
        int hi = hex(value[i + 1]), lo = hex(value[i + 2]);
        if (hi < 0 || lo < 0) return false;
        c = (hi << 4) | lo; i += 2;
      }
      if (c < 32 || c == 127 || ++length > limit) return false;
    }
    return true;
  };
  if (text.empty()) return true;
  for (;;) {
    if (++arguments > HTTP_MAX_ARGUMENTS) return false;
    const auto end = text.find('&');
    const auto part = text.substr(0, end);
    const auto equal = part.find('=');
    if (equal == part.npos || equal == 0 || !decodedSize(part.substr(0, equal), 32) ||
        !decodedSize(part.substr(equal + 1), 128)) return false;
    if (end == text.npos) return true;
    text.remove_prefix(end + 1);
  }
}
inline bool requestGeneration(std::string_view text, uint32_t &value) {
  if (text.empty() || text.size() > 10)
    return false;
  value = 0;
  for (char c : text) {
    if (c < '0' || c > '9' || value > (UINT32_MAX - unsigned(c - '0')) / 10)
      return false;
    value = value * 10 + unsigned(c - '0');
  }
  return true;
}
struct StopRequest {
  int axis = -2;
  uint32_t selection = 0;
  bool scoped = false;
};
// Only STOP and an optional strict selection token bypass allocation-heavy parsing.
inline StopRequest emergencyStopRequest(std::string_view request) {
  const auto end = request.find(" HTTP/");
  auto line = request.substr(0, end);
  StopRequest result;
  const auto at = line.find("&selection=");
  if (at != line.npos) {
    if (!requestGeneration(line.substr(at + 11), result.selection))
      return result;
    result.scoped = true;
    line = line.substr(0, at);
  }
  if (line == "GET /api/move?d=S")
    result.axis = -1;
  else if (line == "GET /api/move?d=A")
    result.axis = 0;
  else if (line == "GET /api/move?d=E")
    result.axis = 1;
  return result;
}
inline int emergencyStopAxis(std::string_view request) { return emergencyStopRequest(request).axis; }
// Inspect the received bytes without allocating strings. The stock
// WebServer is called only once its complete, bounded request is already here.
inline RequestState inspectRequest(std::string_view bytes, size_t capacity, size_t *completeSize = nullptr) {
  const auto end = bytes.find("\r\n\r\n");
  if (end == bytes.npos)
    return bytes.size() >= capacity ? RequestState::TooLarge : RequestState::Incomplete;
  const size_t headerSize = end + 4;
  // The core parser accepts bare CR as a line ending. Require CRLF here so
  // its Content-Length interpretation cannot differ from this envelope.
  for (size_t i = 0; i < headerSize; ++i) {
    const unsigned char ch = bytes[i];
    if (ch == '\r') {
      if (i + 1 >= headerSize || bytes[++i] != '\n') return RequestState::Invalid;
    } else if (ch < 32 && ch != '\t') return RequestState::Invalid;
  }
  size_t pos = bytes.find("\r\n");
  if (pos == 0 || pos == bytes.npos || pos > 512) return RequestState::Invalid;
  const auto requestLine = bytes.substr(0, pos);
  const size_t methodEnd = requestLine.find(' '), urlEnd = requestLine.rfind(' ');
  if (methodEnd == requestLine.npos || urlEnd <= methodEnd + 1 ||
      (requestLine.substr(0, methodEnd) != "GET" && requestLine.substr(0, methodEnd) != "POST") ||
      (requestLine.substr(urlEnd + 1) != "HTTP/1.1" && requestLine.substr(urlEnd + 1) != "HTTP/1.0")) return RequestState::Invalid;
  const auto url = requestLine.substr(methodEnd + 1, urlEnd - methodEnd - 1);
  if (url.empty() || url.front() != '/' || url.find(' ') != url.npos) return RequestState::Invalid;
  const auto query = url.find('?');
  size_t arguments = 0;
  if (query != url.npos && !boundedForm(url.substr(query + 1), arguments)) return RequestState::TooLarge;
  pos += 2;
  unsigned headers = 0;
  size_t bodySize = 0;
  bool hasLength = false;
  while (pos < end) {
    const size_t next = bytes.find("\r\n", pos);
    const auto line = bytes.substr(pos, next - pos);
    if (++headers > 32 || line.size() > 1024) return RequestState::TooLarge;
    const auto colon = line.find(':');
    if (colon == line.npos || colon == 0) return RequestState::Invalid;
    const auto name = line.substr(0, colon);
    if (name.size() > 64) return RequestState::TooLarge;
    for (unsigned char ch : name) if (ch <= 32 || ch >= 127) return RequestState::Invalid;
    auto value = line.substr(colon + 1);
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
    // Neither our API nor WebServer's form handlers use chunked request bodies.
    if (headerEquals(name, "transfer-encoding")) return RequestState::Invalid;
    if (headerEquals(name, "content-type") && headerEquals(value.substr(0, 10), "multipart/"))
      return RequestState::Invalid;
    if (headerEquals(name, "content-length")) {
      if (hasLength || value.empty()) return RequestState::Invalid;
      hasLength = true;
      for (char ch : value) {
        if (ch < '0' || ch > '9') return RequestState::Invalid;
        if (bodySize > capacity / 10) return RequestState::TooLarge;
        bodySize = bodySize * 10 + unsigned(ch - '0');
        if (bodySize > capacity) return RequestState::TooLarge;
      }
    }
    pos = next + 2;
  }
  if (headerSize > capacity || bodySize > capacity - headerSize) return RequestState::TooLarge;
  if (bytes.size() < headerSize + bodySize) return RequestState::Incomplete;
  if (bodySize && (requestLine.substr(0, methodEnd) != "POST" ||
      !boundedForm(bytes.substr(headerSize, bodySize), arguments))) return RequestState::TooLarge;
  if (completeSize) *completeSize = headerSize + bodySize;
  return RequestState::Ready;
}
} // namespace forkrot
