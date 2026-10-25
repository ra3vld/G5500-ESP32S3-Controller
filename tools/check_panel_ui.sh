#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
: "${LVGL_DIR:?Set LVGL_DIR}"
build="${PREVIEW_BUILD_DIR:-build/unified-panel-preview}"
"${CMAKE:-cmake}" -S "$LVGL_DIR" -B "$build/lvgl" -DLV_BUILD_CONF_PATH="$(pwd)/G5500_Fork/lv_conf.h" -DCONFIG_LV_BUILD_EXAMPLES=OFF -DCONFIG_LV_BUILD_DEMOS=OFF -DCMAKE_BUILD_TYPE=Release
"${CMAKE:-cmake}" --build "$build/lvgl" -j "${BUILD_JOBS:-4}"
build_preview() {
  ${CXX:-g++} -std=c++17 -O2 -DBOARD_PROFILE="$1" -DDISPLAY_KIND="$2" -DUSER_BUTTON_GPIO=-1 -DBLUETOOTH_ENABLED=0 -DLV_CONF_INCLUDE_SIMPLE \
    -I G5500_Fork -I "$LVGL_DIR" tools/panel_preview.cpp G5500_Fork/panel_ui.cpp G5500_Fork/panel_fonts.cpp \
    G5500_Fork/crowpanel_font_11.cpp G5500_Fork/crowpanel_font_22.cpp G5500_Fork/crowpanel_font_28.cpp \
    G5500_Fork/crowpanel_font_36.cpp G5500_Fork/crowpanel_font_80.cpp G5500_Fork/crowpanel_font_112.cpp \
    "$build/lvgl/liblvgl.a" -lm -pthread -o "$build/panel-preview-$2"
}
if [ "${1:-}" = --st7796 ]; then
  build_preview 10 5
  "$build/panel-preview-5" "$build" 480 320 0
  exit
fi
build_preview 5 3
"$build/panel-preview-3" "$build" 480 480 1
build_preview 4 1
"$build/panel-preview-1" "$build" 320 240 0
"$build/panel-preview-1" "$build" 240 240 1
build_preview 10 5
"$build/panel-preview-5" "$build" 480 320 0
