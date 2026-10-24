#include "crowpanel_trace.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
using namespace forkrot;
using Action = crowpanel::TouchAction;
static CrowPanelView example() {
  CrowPanelView v;
  v.hud = true; v.dualAxis = true; v.feedback = true; v.targetValid = true;
  v.current = 123.456789f; v.target = 240; v.azDirection = -1;
  v.sourceId = UINT64_MAX; v.speedAvailable[0] = true; v.speedPercent[0] = 75;
  std::snprintf(v.hudName, sizeof v.hudName, "Пульт \"А\"\\Б");
  std::snprintf(v.hudRows[0], sizeof v.hudRows[0], "Строка\nещё\t");
  return v;
}
template<class Trace> static void drain(Trace &trace, uint32_t &now, std::string &wire) {
  unsigned writes = 0;
  while (trace.queued()) {
    assert(++writes < 50000);
    const auto before = trace.queued();
    unsigned calls = 0;
    trace.service(now++, true, [&](const char *data, size_t size) {
      assert(++calls == 1 && size <= 512);
      // No wait or write loop inside one service call, even on zero writes.
      size_t count = writes % 3 == 0 ? 0 : std::min(size, size_t(7));
      wire.append(data, count); return count;
    });
    assert(trace.queued() <= before);
  }
}
int main(int argc, char **argv) {
  const auto v = example(); CrowPanelTraceState state;
  state.online = true; state.axes[0].current = v.current;
  if (argc == 2 && std::string(argv[1]) == "--sample") {
    CrowPanelTrace<> sample; sample.begin(42, "G5500 trace-test", "2026-10-24 12:00:00", "abcdef");
    auto out = [](const char *data, size_t size) { std::cout.write(data, size); return size; };
    sample.service(100, true, out);
    sample.view(v, Action::Mode, state, 101);
    sample.knob(true, true, -2, 102);
    crowpanel::TouchEvent e; e.action = Action::Target; e.axis = 1; e.percent = 33;
    sample.touch(true, true, 120, 70, e, Action::Target, 103);
    for (uint32_t now = 104; sample.queued(); ++now) sample.service(now, true, out);
    return 0;
  }
  CrowPanelTrace<> trace;
  trace.begin(42, "firmware", "build", "elf");
  std::string wire; uint32_t now = 10;
  trace.service(now++, true, [&](const char *data, size_t size) {
    wire.append(data, 3); assert(size >= 3); return size_t(3);
  });
  trace.view(v, Action::None, state, now++);
  trace.knob(true, true, 1, now++);
  assert(trace.queued() == 3);
  crowpanel::TouchEvent touch; touch.action = Action::Mode;
  trace.touch(true, true, 240, 100, touch, Action::Mode, now++);
  assert(trace.queued() == 3 && trace.dropped() == 1 && trace.sequence() == 4);
  drain(trace, now, wire);
  assert(wire.front() == '\n' && wire.back() == '\n');
  assert(wire.find("\"type\":\"meta\"") != std::string::npos);
  assert(wire.find("\"seq\":2") != std::string::npos);
  assert(wire.find("\"seq\":4") == std::string::npos);
  assert(wire.find("\"current\":123.456787") != std::string::npos); // exact float round-trip
  const auto seq = trace.sequence();
  trace.view(v, Action::None, state, now++); // unchanged, before heartbeat
  trace.knob(true, true, 0, now++); // unchanged button
  assert(trace.sequence() == seq);
  trace.knob(true, false, 0, now++);
  drain(trace, now, wire);
  assert(wire.find("\"seq\":5") != std::string::npos);
  assert(wire.find("\"dropped\":1") != std::string::npos);
  // A partial old line is separated before reconnect metadata and snapshot.
  trace.view(v, Action::Confirm, state, now++);
  trace.service(now++, true, [&](const char *data, size_t) { wire.append(data, 5); return size_t(5); });
  trace.service(now++, false, [](const char *, size_t) { assert(false); return size_t(0); });
  assert(trace.queued() == 0 && trace.dropped() == 2);
  const size_t split = wire.size();
  trace.service(now++, true, [&](const char *data, size_t size) { wire.append(data, size); return size; });
  assert(wire.substr(split, 10) == "\n@CPTRACE ");
  trace.view(v, Action::None, state, now++); // reconnect forces unchanged view
  assert(trace.queued() == 2); // retained Confirm snapshot, then changed None snapshot
  drain(trace, now, wire);
  // Whole-record buffer overflow increments loss; no malformed line admitted.
  CrowPanelTrace<300, 1> small;
  small.begin(1, "a", "b", "c");
  small.service(1, true, [](const char *, size_t n) { return n; });
  assert(small.queued() == 0);
  small.view(v, Action::None, state, 2);
  assert(small.queued() == 0 && small.dropped() == 1);
  small.view(v, Action::None, state, 3);
  assert(small.dropped() == 2); // retries unchanged dropped snapshot
  // Worst-case escaping of every text field fits the production fixed frame.
  CrowPanelView escaped;
  auto fill = [](auto &s) { std::memset(s, 1, sizeof s - 1); s[sizeof s - 1] = 0; };
  fill(escaped.hudName); fill(escaped.hudAddress); fill(escaped.ip); fill(escaped.heap); fill(escaped.status);
  for (auto &row : escaped.hudRows) fill(row);
  CrowPanelTrace<> maximum;
  maximum.begin(1, "a", "b", "c");
  maximum.service(UINT32_MAX - 50, true, [](const char *, size_t n) { return n; });
  maximum.view(escaped, Action::None, state, UINT32_MAX - 40);
  assert(maximum.queued() == 1 && maximum.dropped() == 0);
  maximum.service(960, true, [](const char *, size_t) { return size_t(0); }); // wrap-safe metadata heartbeat
  assert(maximum.queued() == 3); // service sends metadata and retained view heartbeat
  maximum.view(escaped, Action::None, state, 961); // heartbeat already covers unchanged view
  assert(maximum.queued() == 3);
  for (unsigned i = 0; i < 1000; ++i)
    maximum.knob(true, false, 1, i + 962);
  assert(maximum.queued() == 3 && maximum.dropped() == 1000);
  CrowPanelTrace<> failure;
  std::string failedWire;
  auto send = [&](const char *p, size_t n) { failedWire.append(p, n); return n; };
  failure.service(1, true, send);
  failure.view(v, Action::None, state, 2);
  failure.displayFailure(true, 3);
  for (uint32_t t = 4; failure.queued(); ++t) failure.service(t, true, send);
  assert(failedWire.find("\"displayFailed\":true") != std::string::npos);
  failure.service(20, false, send);
  failedWire.clear();
  failure.service(21, true, send);
  for (uint32_t t = 22; failure.queued(); ++t) failure.service(t, true, send);
  assert(failedWire.find("\"displayFailed\":true") != std::string::npos);
  assert(failedWire.find("\"type\":\"view\"") != std::string::npos);
  std::cout << "PASS: trace full snapshots, escaping, partial/zero writes, drops, reconnect, and wrap-safe heartbeats\n";
}
