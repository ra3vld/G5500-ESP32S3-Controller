#include "api.hpp"
#include "write_budget.hpp"
#include "byte_log.hpp"
#include "http_request_gate.hpp"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <limits>

static bool denyAllocations = false;
void *operator new(size_t size) {
  if (denyAllocations) throw std::bad_alloc();
  if (void *p = std::malloc(size ? size : 1)) return p;
  throw std::bad_alloc();
}
void *operator new[](size_t size) { return ::operator new(size); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, size_t) noexcept { std::free(p); }
void operator delete[](void *p, size_t) noexcept { std::free(p); }

using namespace forkrot;
int main() {
  Controller controller;
  Protocol protocol(controller);
  Settings settings;
  protocol.rotorName = "Россия\"\\\n";
  settings.reportHost = "192.168.1.254";
  controller.axes[0].current = 380.25;
  controller.pending = {12.3456f, 0.000001f, true, true};
  controller.pendingValid = true;
  for (auto &preset : controller.presets) preset.label = std::string(32, '\x01');
  const auto expected = stateJson(controller, protocol, settings, 42);
  char buffer[STATUS_JSON_CAPACITY];
  for (unsigned i = 0; i < 10000; ++i) {
    JsonWriter json(buffer, sizeof buffer);
    denyAllocations = true;
    const bool ok = writeStateJson(json, controller, protocol, settings, 42);
    denyAllocations = false;
    assert(ok);
    assert(json.size() == expected.size());
    assert(std::memcmp(json.data(), expected.data(), expected.size()) == 0);
  }
  assert(expected.find(jsonQuote(protocol.positionReport(controller.axes[0].current))) != std::string::npos);
  assert(expected.find("12.3456") != std::string::npos);
  JsonWriter joined(buffer, sizeof buffer);
  joined << "{\"legacy\":true";
  denyAllocations = true;
  const bool joinedOk = writeStateJson(joined, controller, protocol, settings, 42, true);
  denyAllocations = false;
  assert(joinedOk);
  assert(std::string(joined.data()) == "{\"legacy\":true," + expected.substr(1));
  // Keep room for the full device prefix, including escaped SSID/calibration
  // fields. 1900 bytes exceeds its conservative 1260-byte measured fixture.
  const auto originalName = protocol.rotorName;
  protocol.rotorName = std::string(32, '\x01');
  char prefix[1900];std::memset(prefix,' ',sizeof prefix);
  JsonWriter fullStatus(buffer,sizeof buffer);
  denyAllocations = true;
  fullStatus << "{\"prefix\":" << jsonQuoted({prefix,sizeof prefix});
  const bool fullOk=writeStateJson(fullStatus,controller,protocol,settings,42,true);
  denyAllocations = false;
  assert(fullOk && fullStatus.size()<STATUS_JSON_CAPACITY);
  protocol.rotorName=originalName;
  char small[10];
  std::memset(small, 'X', sizeof small);
  JsonWriter overflow(small + 1, 8);
  denyAllocations = true;
  const bool smallOk = writeStateJson(overflow, controller, protocol, settings, 42);
  denyAllocations = false;
  assert(!smallOk && small[0] == 'X' && small[9] == 'X');
  assert(small[1 + overflow.size()] == 0);
  // Preserve printf-compatible precision and rounding while avoiding newlib's
  // per-task dtoa allocations. Exercise binary fractions and exponent edges.
  const auto checkDecimal = [&](double value, int precision) {
    char reference[128];
    const int size = precision < 0 ? std::snprintf(reference, sizeof reference, "%.6g", value)
                                   : std::snprintf(reference, sizeof reference, "%.*f", precision, value);
    JsonWriter output(buffer, sizeof buffer);
    denyAllocations = true;
    output.decimal(value, precision);
    denyAllocations = false;
    assert(output.good() == (size >= 0 && size < 64));
    if (output.good()) assert(std::strcmp(output.data(), reference) == 0);
  };
  for (const double value : {0., -0., 0.125, 1.005, -12.3456, 359.995, 450., 1e-12, 1e20,
                             std::numeric_limits<double>::denorm_min(), std::numeric_limits<double>::max()})
    for (const int precision : {-1, 0, 1, 2, 6}) checkDecimal(value, precision);
  uint32_t random = 7;
  for (unsigned i = 0; i < 10000; ++i) {
    random = random * 1664525U + 1013904223U;
    const double value = (double(random) - 2147483648.) / 987654.321;
    checkDecimal(value, 2);checkDecimal(value, -1);
  }
  JsonWriter nonfinite(buffer,sizeof buffer);
  nonfinite << std::numeric_limits<double>::infinity() << ',' << std::numeric_limits<double>::quiet_NaN();
  assert(nonfinite.good() && std::strcmp(nonfinite.data(),"null,null")==0);

  char data[32768] = {};
  uint32_t now = 0;
  auto clock = [&] { return now; };
  auto pause = [&] { ++now; };
  assert(writeWithinBudget(data, sizeof data, 0, 250,
      [](const char *, size_t size) { return int(size); }, clock, pause) == sizeof data);
  assert(writeWithinBudget(data, sizeof data, 0, 250,
      [](const char *, size_t) { return -2; }, clock, pause) == 0);
  assert(now == 250);
  now = 0;
  const auto partial = writeWithinBudget(data, sizeof data, 0, 250,
      [&](const char *, size_t) { now += 10; return 1; }, clock, pause);
  assert(partial == 25 && now == 250); // progress must not keep renewing a deadline
  now = UINT32_MAX - 100;
  const auto start = now;
  assert(writeWithinBudget(data, sizeof data, start, 250,
      [](const char *, size_t) { return -2; }, clock, pause) == 0);
  assert(uint32_t(now - start) == 250);
  now = 0;
  assert(writeWithinBudget(data, sizeof data, 0, 250,
      [](const char *, size_t) { return -1; }, clock, pause) == 0 && now == 0);
  RestartDelay restart;
  restart.request(0);
  assert(!restart.consume(1499));
  restart.request(1400);
  assert(restart.consume(1500) && !restart.consume(1600));
  restart.request(UINT32_MAX - 100);
  assert(!restart.consume(100));
  assert(restart.consume(1400));
  ByteLog<32, 8> log;
  std::string snapshot;
  log.append("one\r\npart");
  log.append("ial", true);
  log.copyTo(snapshot);
  assert(snapshot == "one\npartial\n");
  log.clear(); snapshot.clear();
  log.append("123456789", true); log.copyTo(snapshot);
  assert(snapshot == "12345678\n9\n");
  log.clear(); snapshot.clear();
  log.append("12345\nabcdefg\n1234567\nABCDEFG\n"); log.copyTo(snapshot);
  assert(snapshot == "12345\nabcdefg\n1234567\nABCDEFG\n");
  log.append("end\n"); snapshot.clear(); log.copyTo(snapshot);
  assert(snapshot == "abcdefg\n1234567\nABCDEFG\nend\n");
  // Filling and rotating the device's byte log never asks for heap memory.
  ByteLog<2048> deviceLog;
  denyAllocations = true;
  for (unsigned i = 0; i < 10000; ++i) deviceLog.append("[WIFI] reconnect timeout -> restore AP ONLY", true);
  denyAllocations = false;
  assert(deviceLog.size() <= 2048);
  snapshot.clear(); deviceLog.copyTo(snapshot);
  assert(snapshot.find("[WIFI]") == 0 && snapshot.back() == '\n');
  char logBuffer[2049];
  denyAllocations = true;
  const size_t logSize = deviceLog.copyTo(logBuffer, sizeof logBuffer);
  denyAllocations = false;
  assert(logSize == snapshot.size() && std::string(logBuffer) == snapshot);
  assert(deviceLog.copyTo(logBuffer, logSize) == 0 && logBuffer[0] == 0);
  const std::string getRequest = "GET /api/status HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n";
  const std::string wifiRequest = "POST /api/wifi HTTP/1.1\r\nContent-Type: application/x-www-form-urlencoded\r\ncOnTeNt-LeNgTh: 8\r\n\r\nssid=abc";
  denyAllocations = true;
  for (unsigned n = 0; n < 10000; ++n) {
    assert(inspectRequest(getRequest, 4096) == RequestState::Ready);
    assert(inspectRequest(wifiRequest, 4096) == RequestState::Ready);
    for (size_t i = 0; i < wifiRequest.size(); ++i)
      assert(inspectRequest(std::string_view(wifiRequest).substr(0, i), 4096) == RequestState::Incomplete);
  }
  assert(inspectRequest("GET / HTTP/1.1\r\nContent-Length: -1\r\n\r\n",4096) == RequestState::Invalid);
  assert(inspectRequest("GET / HTTP/1.1\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\n",4096) == RequestState::Invalid);
  assert(inspectRequest("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n",4096) == RequestState::Invalid);
  assert(inspectRequest("POST / HTTP/1.1\r\nContent-Length: 99999999999999999999999999\r\n\r\n",4096) == RequestState::TooLarge);
  assert(inspectRequest("POST / HTTP/1.1\r\nContent-Length: 4096\r\n\r\n",4096) == RequestState::TooLarge);
  assert(inspectRequest("GET / HTTP/1.1\r\nBad header\r\n\r\n",4096) == RequestState::Invalid);
  assert(inspectRequest("GET / HTTP/1.1\r\nHost: x",20) == RequestState::TooLarge);
  denyAllocations = false;
  std::cout << "PASS: 10000 status serializations with C++ allocations forbidden; overflow, partial/EAGAIN writes, deadlines and deferred restart\n";
  std::cout << "PASS: fixed byte log, split lines, wrap, complete-line eviction and 10000 heap-free appends\n";
  std::cout << "PASS: HTTP envelope, fragmented headers/body and 10000 heap-free request preflights\n";
}
