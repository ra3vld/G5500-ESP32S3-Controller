// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include "json_writer.hpp"
#include "crowpanel_view.hpp"
#include "crowpanel_touch.hpp"
#include <array>
#include <limits>

namespace forkrot {
inline void writeCrowPanelReal(JsonWriter &j, float value) {
  if (!std::isfinite(value)) { j << "null"; return; }
  char number[32];
  const auto result = std::to_chars(number, number + sizeof number, value,
      std::chars_format::general, std::numeric_limits<float>::max_digits10);
  if (result.ec == std::errc()) j << std::string_view(number, size_t(result.ptr - number));
  else j << "null";
}
// Complete, allocation-free snapshots. Keep field names equal to CrowPanelView.
inline void writeCrowPanelView(JsonWriter &j, const CrowPanelView &v) {
  j << '{';
#define CP_BOOL(name) j << "\"" #name "\":" << (v.name ? "true" : "false") << ','
#define CP_NUMBER(name) j << "\"" #name "\":" << v.name << ','
#define CP_FLOAT(name) j << "\"" #name "\":"; writeCrowPanelReal(j, v.name); j << ','
#define CP_TEXT(name) j << "\"" #name "\":" << jsonQuoted(v.name) << ','
  CP_BOOL(movingNow); CP_BOOL(settingsAvailable); CP_BOOL(enabled);
  CP_BOOL(hud); CP_BOOL(hudSelector); CP_TEXT(hudName); CP_TEXT(hudAddress);
  j << "\"hudRows\":[" << jsonQuoted(v.hudRows[0]) << ',' << jsonQuoted(v.hudRows[1]) << ','
    << jsonQuoted(v.hudRows[2]) << "],";
  CP_BOOL(dualAxis); CP_BOOL(speedScreen);
  j << "\"speedAvailable\":[" << (v.speedAvailable[0] ? "true" : "false") << ','
    << (v.speedAvailable[1] ? "true" : "false") << "],\"speedPercent\":["
    << v.speedPercent[0] << ',' << v.speedPercent[1] << "],";
  CP_BOOL(elevation); CP_BOOL(feedback); CP_BOOL(targetValid);
  CP_BOOL(uncalibrated); CP_BOOL(secondaryUncalibrated);
  CP_BOOL(pending); CP_BOOL(moving); CP_BOOL(autoMode); CP_BOOL(autoHour);
  CP_NUMBER(direction); CP_FLOAT(current); CP_FLOAT(target); CP_FLOAT(minimum); CP_FLOAT(maximum);
  CP_FLOAT(secondaryCurrent); CP_FLOAT(secondaryTarget);
  CP_BOOL(secondaryFeedback); CP_BOOL(secondaryTargetValid); CP_BOOL(secondaryPending);
  CP_NUMBER(azDirection); CP_NUMBER(elDirection); CP_NUMBER(sourceId);
  CP_BOOL(wifiConnected); CP_BOOL(wifiAp); CP_NUMBER(wifiRssi);
  CP_BOOL(bluetoothEnabled); CP_BOOL(bluetoothConnected); CP_TEXT(ip); CP_TEXT(heap);
  CP_BOOL(statusFault);
  j << "\"status\":" << jsonQuoted(v.status) << '}';
#undef CP_BOOL
#undef CP_NUMBER
#undef CP_FLOAT
#undef CP_TEXT
}
struct CrowPanelTraceState {
  int model = 0, mode = 0;
  bool simulation = false, online = false, displayFailed = false;
  struct Axis {
    float current = 0, target = 0, outputPercent = 0, measuredVelocity = 0, requestedVelocity = 0;
    bool calibrated = false, targetKnown = false;
    int direction = 0, motionPhase = 0, motionError = 0;
  } axes[2];
};
inline void writeCrowPanelTraceState(JsonWriter &j, const CrowPanelTraceState &s) {
  j << "{\"model\":" << s.model << ",\"mode\":" << s.mode
    << ",\"simulation\":" << (s.simulation ? "true" : "false")
    << ",\"online\":" << (s.online ? "true" : "false")
    << ",\"displayFailed\":" << (s.displayFailed ? "true" : "false") << ",\"axes\":[";
  for (unsigned i = 0; i < 2; ++i) {
    const auto &a = s.axes[i];
    if (i) j << ',';
    j << "{\"current\":"; j.decimal(a.current, -1);
    j << ",\"target\":"; j.decimal(a.target, -1);
    j << ",\"calibrated\":" << (a.calibrated ? "true" : "false")
      << ",\"targetKnown\":" << (a.targetKnown ? "true" : "false")
      << ",\"direction\":" << a.direction << ",\"motionPhase\":" << a.motionPhase
      << ",\"motionError\":" << a.motionError << ",\"outputPercent\":";
    j.decimal(a.outputPercent, -1) << ",\"measuredVelocity\":";
    j.decimal(a.measuredVelocity, -1) << ",\"requestedVelocity\":";
    j.decimal(a.requestedVelocity, -1) << '}';
  }
  j << "]}";
}
// Fixed memory, at most one bounded write per service call. A full queue drops
// whole new records; a partially sent line always retains its original bytes.
// On reconnect a newline and fresh metadata delimit any truncated old line.
// Capacity includes worst-case JSON escaping of every fixed-size view string.
template<size_t Capacity = 8192, size_t Slots = 3> class CrowPanelTrace {
  static_assert(Capacity > 256 && Slots > 0, "Invalid trace queue capacity");
  struct Frame { std::array<char, Capacity> bytes{}; size_t size = 0, offset = 0; };
  std::array<Frame, Slots> queue_{};
  size_t head_ = 0, count_ = 0;
  uint64_t seq_ = 0, dropped_ = 0;
  uint32_t lastMeta_ = 0, lastView_ = 0, lastViewAttempt_ = 0, viewHash_ = 0;
  CrowPanelView latestView_{};
  CrowPanelTraceState latestState_{};
  crowpanel::TouchAction latestPressed_ = crowpanel::TouchAction::None;
  bool latestValid_ = false;
  bool connected_ = false, viewValid_ = false, forceView_ = true;
  char boot_[17] = "0", firmware_[80]{}, build_[48]{}, elf_[65]{};
  bool knobValid_ = false, knobDown_ = false, knobHealthy_ = false;
  bool touchValid_ = false, touchDown_ = false, touchHealthy_ = false;
  uint16_t touchX_ = 0, touchY_ = 0;
  crowpanel::TouchAction action_ = crowpanel::TouchAction::None, pressed_ = crowpanel::TouchAction::None;
  uint32_t gesture_ = 0;
  uint8_t mask_ = 0;
  bool manual_ = false, release_ = false;
  float az_ = 0, el_ = 0;
  template<class Fill> bool enqueue(const char *type, uint32_t now, Fill fill, bool resync = false) {
    const uint64_t seq = ++seq_;
    if (!connected_ || count_ == Slots) { ++dropped_; return false; }
    auto &frame = queue_[(head_ + count_) % Slots];
    JsonWriter j(frame.bytes.data(), frame.bytes.size());
    if (resync) j << '\n';
    j << "@CPTRACE {\"schema\":\"crowpanel.trace\",\"v\":1,\"type\":" << jsonQuoted(type)
      << ",\"boot\":" << jsonQuoted(boot_) << ",\"seq\":" << seq << ",\"ms\":" << now
      << ",\"dropped\":" << dropped_;
    fill(j);
    j << "}\n";
    if (!j.good()) { ++dropped_; return false; }
    frame.size = j.size(); frame.offset = 0; ++count_; return true;
  }
  void discard() { dropped_ += count_; count_ = 0; head_ = 0; }
public:
  void begin(uint64_t boot, const char *firmware, const char *build, const char *elf) {
    std::snprintf(boot_, sizeof boot_, "%016llx", static_cast<unsigned long long>(boot));
    std::snprintf(firmware_, sizeof firmware_, "%s", firmware);
    std::snprintf(build_, sizeof build_, "%s", build);
    std::snprintf(elf_, sizeof elf_, "%s", elf);
  }
  uint64_t dropped() const { return dropped_; }
  uint64_t sequence() const { return seq_; }
  size_t queued() const { return count_; }
  template<class Write> void service(uint32_t now, bool connected, Write write) {
    const bool reconnect = connected && !connected_;
    if (connected != connected_) {
      discard(); connected_ = connected; forceView_ = true;
      // The first sample after reconnect also describes held/released controls.
      knobValid_ = touchValid_ = false;
    }
    if (!connected_) return;
    if (reconnect || uint32_t(now - lastMeta_) >= 1000) {
      lastMeta_ = now;
      enqueue("meta", now, [&](JsonWriter &j) {
        j << ",\"firmware\":" << jsonQuoted(firmware_) << ",\"build\":" << jsonQuoted(build_)
          << ",\"elfSha256\":" << jsonQuoted(elf_) << ",\"width\":480,\"height\":480";
      }, reconnect);
    }
    // Preserve the last software image even when display failure stops draws.
    // A reconnect and a periodic heartbeat then remain independently replayable.
    if (latestValid_ && (reconnect || (uint32_t(now - lastView_) >= 1000 &&
                                     uint32_t(now - lastViewAttempt_) >= 1000)))
      view(latestView_, latestPressed_, latestState_, now);
    if (!count_) return;
    auto &frame = queue_[head_];
    const size_t offered = std::min(size_t(512), frame.size - frame.offset);
    const size_t sent = std::min(offered, write(frame.bytes.data() + frame.offset, offered));
    frame.offset += sent;
    if (frame.offset == frame.size) { head_ = (head_ + 1) % Slots; --count_; }
  }
  void view(const CrowPanelView &v, crowpanel::TouchAction pressed, const CrowPanelTraceState &state, uint32_t now) {
    latestView_ = v; latestState_ = state; latestPressed_ = pressed; latestValid_ = true;
    if (!connected_) { ++seq_; ++dropped_; forceView_ = true; return; }
    char unused[1]; JsonWriter fingerprint(unused, sizeof unused); fingerprint.page(0);
    writeCrowPanelView(fingerprint, v); fingerprint << int(pressed); writeCrowPanelTraceState(fingerprint, state);
    const auto hash = fingerprint.fingerprint();
    if (!forceView_ && viewValid_ && hash == viewHash_ && uint32_t(now - lastView_) < 1000) return;
    viewHash_ = hash; viewValid_ = true;
    // Retry a dropped view on the next draw, including when it was unchanged.
    lastViewAttempt_ = now;
    forceView_ = !enqueue("view", now, [&](JsonWriter &j) {
      j << ",\"pressed\":" << int(pressed) << ",\"view\":"; writeCrowPanelView(j, v);
      j << ",\"state\":"; writeCrowPanelTraceState(j, state);
    });
    if (!forceView_) lastView_ = now;
  }
  void displayFailure(bool failed, uint32_t now) {
    if (!latestValid_ || latestState_.displayFailed == failed) return;
    latestState_.displayFailed = failed;
    view(latestView_, latestPressed_, latestState_, now);
  }
  void knob(bool healthy, bool down, int delta, uint32_t now) {
    if (knobValid_ && healthy == knobHealthy_ && down == knobDown_ && delta == 0) return;
    knobValid_ = true; knobHealthy_ = healthy; knobDown_ = down;
    enqueue("input", now, [&](JsonWriter &j) {
      j << ",\"input\":{\"kind\":\"knob\",\"healthy\":" << (healthy ? "true" : "false")
        << ",\"down\":" << (down ? "true" : "false") << ",\"delta\":" << delta << '}';
    });
  }
  void touch(bool healthy, bool down, uint16_t x, uint16_t y, const crowpanel::TouchEvent &e,
             crowpanel::TouchAction pressed, uint32_t now) {
    if (touchValid_ && healthy == touchHealthy_ && down == touchDown_ && (!down || (x == touchX_ && y == touchY_)) &&
        e.action == action_ && pressed == pressed_ && e.gesture == gesture_ && e.mask == mask_ &&
        e.manual == manual_ && e.release == release_ && e.az == az_ && e.el == el_) return;
    touchValid_ = true; touchHealthy_ = healthy; touchDown_ = down; touchX_ = x; touchY_ = y;
    action_ = e.action; pressed_ = pressed; gesture_ = e.gesture; mask_ = e.mask;
    manual_ = e.manual; release_ = e.release; az_ = e.az; el_ = e.el;
    enqueue("input", now, [&](JsonWriter &j) {
      j << ",\"pressed\":" << int(pressed) << ",\"input\":{\"kind\":\"touch\",\"healthy\":"
        << (healthy ? "true" : "false") << ",\"down\":" << (down ? "true" : "false")
        << ",\"x\":" << x << ",\"y\":" << y << ",\"action\":" << int(e.action)
        << ",\"manual\":" << (e.manual ? "true" : "false") << ",\"release\":" << (e.release ? "true" : "false")
        << ",\"gesture\":" << e.gesture << ",\"mask\":" << e.mask << ",\"az\":";
      j.decimal(e.az, -1) << ",\"el\":"; j.decimal(e.el, -1);
      j << ",\"axis\":" << e.axis << ",\"percent\":" << e.percent
        << ",\"absoluteSpeed\":" << (e.absoluteSpeed ? "true" : "false") << '}';
    });
  }
};
}
