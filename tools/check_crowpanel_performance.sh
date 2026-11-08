#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
: "${LVGL_DIR:?Set LVGL_DIR}"
build="${PREVIEW_BUILD_DIR:-build/unified-panel-preview}"
inputs="${PERFORMANCE_INPUT_DIR:-G5500_Fork}"
${CXX:-g++} -std=c++17 -O2 -DBOARD_PROFILE=5 -DDISPLAY_KIND=3 -DDRIVE_INTERFACE=0 \
 -DUSER_BUTTON_GPIO=-1 -DBLUETOOTH_ENABLED=0 -DLV_CONF_INCLUDE_SIMPLE -I "$inputs" -I G5500_Fork -I "$LVGL_DIR" \
 tools/crowpanel_performance.cpp "$inputs/panel_ui.cpp" G5500_Fork/panel_fonts.cpp \
 G5500_Fork/crowpanel_font_11.cpp G5500_Fork/crowpanel_font_22.cpp G5500_Fork/crowpanel_font_28.cpp \
 G5500_Fork/crowpanel_font_36.cpp G5500_Fork/crowpanel_font_80.cpp G5500_Fork/crowpanel_font_112.cpp \
 "$build/lvgl/liblvgl.a" -lm -pthread -o "$build/performance"
"$build/performance" "$@"
