#!/bin/sh
# Build the actual firmware LVGL UI for headless desktop replay.
set -eu
cd "$(dirname "$0")/.."
root="$(pwd)"
build="${TRACE_BUILD_DIR:-$root/build/crowpanel-trace}"
if [ -z "${LVGL_DIR:-}" ]; then
  for candidate in "$root/.pio/libdeps/crowpanel_21/lvgl" "$root/.pio/libdeps/crowpanel_21_trace/lvgl" "$root/.pio/libdeps/crowpanel_21_hud/lvgl"; do
    if [ -f "$candidate/CMakeLists.txt" ]; then LVGL_DIR="$candidate"; break; fi
  done
fi
: "${LVGL_DIR:?Set LVGL_DIR to the LVGL 9.6.0 source directory (or install project dependencies)}"
cmake_bin="${CMAKE:-}"
if [ -z "$cmake_bin" ]; then
  cmake_bin="$(command -v cmake || true)"
fi
if [ -z "$cmake_bin" ]; then
  for candidate in "$HOME"/.cache/esphome/idf/tools/cmake/*/bin/cmake "$HOME"/.platformio/packages/tool-cmake/bin/cmake; do
    if [ -x "$candidate" ]; then cmake_bin="$candidate"; break; fi
  done
fi
if [ -z "$cmake_bin" ]; then
  echo "CMake not found. Install CMake or set CMAKE=/path/to/cmake." >&2
  exit 1
fi
mkdir -p "$build"
"$cmake_bin" -S "$LVGL_DIR" -B "$build/lvgl" \
  -DLV_BUILD_CONF_PATH="$root/G5500_Fork/lv_conf.h" \
  -DCONFIG_LV_BUILD_EXAMPLES=OFF -DCONFIG_LV_BUILD_DEMOS=OFF \
  -DCMAKE_BUILD_TYPE=Release >&2
"$cmake_bin" --build "$build/lvgl" -j "${BUILD_JOBS:-4}" >&2
${CXX:-g++} -std=c++17 -O2 -DBOARD_PROFILE=5 -DDISPLAY_KIND=3 \
  -DDRIVE_INTERFACE=0 -DUSER_BUTTON_GPIO=-1 -DBLUETOOTH_ENABLED=0 -DLV_CONF_INCLUDE_SIMPLE \
  -I G5500_Fork -I "$LVGL_DIR" tools/crowpanel_trace_renderer.cpp G5500_Fork/panel_ui.cpp G5500_Fork/panel_fonts.cpp \
  G5500_Fork/crowpanel_font_11.cpp G5500_Fork/crowpanel_font_22.cpp \
  G5500_Fork/crowpanel_font_36.cpp G5500_Fork/crowpanel_font_112.cpp \
  G5500_Fork/crowpanel_font_28.cpp G5500_Fork/crowpanel_font_80.cpp \
  "$build/lvgl/liblvgl.a" -lm -pthread -o "$build/renderer"
printf '%s\n' "$build/renderer"
