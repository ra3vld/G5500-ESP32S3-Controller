// Native replay backend. Render the firmware's actual LVGL objects and fonts.
// Private stdin protocol: RESET; VIEW<TAB>time_ms<TAB>pressed<TAB>name=value...;
// PRESSED<TAB>time_ms<TAB>action; TOUCH/KNOB carry raw input samples.
// FRAME writes 480*480*3 RGB bytes; FRAME_INPUTS writes 600*600*3 with gestures
// and a virtual encoder ring. Strings are UTF-8 bytes encoded as hex.
#include "crowpanel_ui.hpp"
#include "crowpanel_trace_inputs.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr size_t size = 480;
std::array<uint16_t, size * size> pixels{};
std::array<uint16_t, size * 40> stripe{};
std::array<uint8_t, size * size * 3> rgb{};
lv_display_t *display = nullptr;
forkrot::CrowPanelUi ui;
uint64_t cursor = 0;
bool initialized = false;
traceviz::Inputs inputs;

void flush(lv_display_t *d, const lv_area_t *a, uint8_t *data) {
  auto *p = reinterpret_cast<uint16_t *>(data);
  for (int y = a->y1; y <= a->y2; ++y)
    for (int x = a->x1; x <= a->x2; ++x) {
      if (x < 0 || x >= int(size) || y < 0 || y >= int(size)) std::abort();
      pixels[size_t(y) * size + size_t(x)] = *p++;
    }
  lv_display_flush_ready(d);
}

void reset() {
  if (initialized) lv_deinit();
  ui = forkrot::CrowPanelUi{};
  pixels.fill(0);
  cursor = 0;
  inputs.reset();
  lv_init();
  initialized = true;
  display = lv_display_create(size, size);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display, stripe.data(), nullptr, sizeof stripe, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, flush);
  ui.create();
  ui.update(forkrot::CrowPanelView{});
  lv_refr_now(display);
}

uint64_t natural(const std::string &value) {
  if (value.empty() || value[0] == '-') throw std::runtime_error("negative/empty unsigned value");
  size_t end = 0;
  const auto result = std::stoull(value, &end);
  if (end != value.size()) throw std::runtime_error("invalid unsigned value");
  return result;
}

void advance(uint64_t requested) {
  if (requested < cursor) throw std::runtime_error("time moved backwards without RESET");
  // Tick the actual LVGL animation and refresh timers between every source event.
  // Timer boundaries stay on a fixed grid, independent of output FPS and seeks.
  while (cursor < requested) {
    const auto step = uint32_t(std::min<uint64_t>(5 - cursor % 5, requested - cursor));
    lv_tick_inc(step);
    cursor += step;
    if (cursor % 5 == 0) lv_timer_handler();
  }
  // A source update can restart a marker's 160 ms animation. Sample the old
  // animation at this exact event time so the result is independent of the
  // number of intermediate frames requested by a viewer or video exporter.
  lv_anim_refr_now();
}

bool boolean(const std::string &value) {
  if (value == "1") return true;
  if (value == "0") return false;
  throw std::runtime_error("invalid boolean");
}
int integer(const std::string &value) {
  size_t end = 0;
  const int result = std::stoi(value, &end);
  if (end != value.size()) throw std::runtime_error("invalid integer");
  return result;
}
float real(const std::string &value) {
  if (value == "null") return std::numeric_limits<float>::quiet_NaN();
  size_t end = 0;
  const float result = std::stof(value, &end);
  if (end != value.size() || !std::isfinite(result)) throw std::runtime_error("invalid float");
  return result;
}
unsigned digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  throw std::runtime_error("invalid string hex");
}
template <size_t N> void stringValue(char (&target)[N], const std::string &value) {
  if (value.size() % 2 || value.size() / 2 >= N) throw std::runtime_error("invalid string size");
  std::memset(target, 0, N);
  for (size_t i = 0; i < value.size() / 2; ++i) {
    target[i] = char(digit(value[i * 2]) * 16 + digit(value[i * 2 + 1]));
    if (!target[i]) throw std::runtime_error("embedded string NUL");
  }
}
void field(forkrot::CrowPanelView &v, const std::string &key, const std::string &value) {
#define BOOL(name) if (key == #name) { v.name = boolean(value); return; }
#define INT(name) if (key == #name) { v.name = integer(value); return; }
#define UINT(name) if (key == #name) { const auto n = natural(value); if (n > UINT32_MAX) throw std::runtime_error("unsigned overflow"); v.name = unsigned(n); return; }
#define REAL(name) if (key == #name) { v.name = real(value); return; }
#define TEXT(name) if (key == #name) { stringValue(v.name, value); return; }
  BOOL(hud) BOOL(hudSelector) TEXT(hudName) TEXT(hudAddress)
  TEXT(hudRows[0]) TEXT(hudRows[1]) TEXT(hudRows[2])
  BOOL(dualAxis) BOOL(speedScreen)
  BOOL(speedAvailable[0]) BOOL(speedAvailable[1])
  UINT(speedPercent[0]) UINT(speedPercent[1])
  BOOL(elevation) BOOL(feedback) BOOL(targetValid)
  BOOL(uncalibrated) BOOL(secondaryUncalibrated)
  BOOL(pending) BOOL(moving) BOOL(autoMode) BOOL(autoHour)
  INT(direction) REAL(current) REAL(target) REAL(minimum) REAL(maximum)
  REAL(secondaryCurrent) REAL(secondaryTarget)
  BOOL(secondaryFeedback) BOOL(secondaryTargetValid) BOOL(secondaryPending)
  INT(azDirection) INT(elDirection)
  if (key == "sourceId") { v.sourceId = natural(value); return; }
  BOOL(wifiConnected) BOOL(wifiAp) INT(wifiRssi)
  BOOL(bluetoothEnabled) BOOL(bluetoothConnected)
  TEXT(ip) TEXT(heap) BOOL(statusFault) TEXT(status)
#undef BOOL
#undef INT
#undef UINT
#undef REAL
#undef TEXT
  throw std::runtime_error("unknown view field: " + key);
}
void pressed(const std::string &value) {
  const int action = integer(value);
  if (action < 0 || action > int(forkrot::crowpanel::TouchAction::SpeedValue))
    throw std::runtime_error("invalid pressed action");
  ui.setPressed(static_cast<forkrot::crowpanel::TouchAction>(action));
}

void frame(bool showInputs = false) {
  lv_refr_now(display);
  for (unsigned y = 0; y < size; ++y) for (unsigned x = 0; x < size; ++x) {
    auto c = pixels[y * size + x];
    if ((int(x) - 240) * (int(x) - 240) + (int(y) - 240) * (int(y) - 240) > 240 * 240) c = 0;
    auto *out = rgb.data() + (y * size + x) * 3;
    out[0] = uint8_t(((c >> 11) & 31) * 255 / 31);
    out[1] = uint8_t(((c >> 5) & 63) * 255 / 63);
    out[2] = uint8_t((c & 31) * 255 / 31);
  }
  const auto *bytes = rgb.data();
  size_t count = rgb.size();
  if (showInputs) { const auto &annotated = inputs.render(bytes, cursor); bytes = annotated.data(); count = annotated.size(); }
  if (std::fwrite(bytes, 1, count, stdout) != count || std::fflush(stdout))
    throw std::runtime_error("frame pipe closed");
}
std::vector<std::string> split(const std::string &line) {
  std::vector<std::string> result;
  size_t start = 0, end;
  do {
    end = line.find('\t', start);
    result.push_back(line.substr(start, end == std::string::npos ? end : end - start));
    start = end + 1;
  } while (end != std::string::npos);
  return result;
}
}

int main() {
  try {
    reset();
    std::string line;
    while (std::getline(std::cin, line)) {
      const auto args = split(line);
      if (args[0] == "RESET" && args.size() == 1) reset();
      else if (args[0] == "FRAME" && args.size() == 2) { advance(natural(args[1])); frame(); }
      else if (args[0] == "FRAME_INPUTS" && args.size() == 2) { advance(natural(args[1])); frame(true); }
      else if (args[0] == "CLEAR_INPUTS" && args.size() == 2) { advance(natural(args[1])); inputs.clear(cursor); }
      else if (args[0] == "TOUCH" && args.size() == 6) {
        advance(natural(args[1]));
        const auto x = natural(args[4]), y = natural(args[5]);
        if (x >= 480 || y >= 480) throw std::runtime_error("touch coordinates out of range");
        inputs.touch(cursor, boolean(args[2]), boolean(args[3]), unsigned(x), unsigned(y));
      }
      else if (args[0] == "KNOB" && args.size() == 5) {
        advance(natural(args[1])); inputs.knob(cursor, boolean(args[2]), boolean(args[3]), integer(args[4]));
      }
      else if (args[0] == "PRESSED" && args.size() == 3) { advance(natural(args[1])); pressed(args[2]); }
      else if (args[0] == "VIEW" && args.size() >= 3) {
        advance(natural(args[1]));
        forkrot::CrowPanelView view;
        for (size_t i = 3; i < args.size(); ++i) {
          const auto equal = args[i].find('=');
          if (equal == std::string::npos) throw std::runtime_error("invalid view field");
          field(view, args[i].substr(0, equal), args[i].substr(equal + 1));
        }
        ui.update(view);
        pressed(args[2]);
      } else throw std::runtime_error("invalid renderer command");
    }
    if (initialized) lv_deinit();
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "CrowPanel renderer: %s\n", error.what());
    return 1;
  }
}
