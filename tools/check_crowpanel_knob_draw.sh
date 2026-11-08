#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
: "${LVGL_DIR:?Set LVGL_DIR}"
build="${PREVIEW_BUILD_DIR:-build/unified-panel-preview}"
sh tools/build_ruckig_native.sh
${CXX:-g++} -std=c++17 -O2 -DDRIVE_SIMULATION=0 -DBOARD_PROFILE=5 -DDISPLAY_KIND=3 \
  -DUSER_BUTTON_GPIO=-1 -DBLUETOOTH_ENABLED=0 -DLV_CONF_INCLUDE_SIMPLE \
  -I tests -I G5500_Fork -I "$LVGL_DIR" \
  tools/crowpanel_knob_draw_tests.cpp G5500_Fork/control.cpp G5500_Fork/motion.cpp \
  G5500_Fork/protocol.cpp G5500_Fork/hud.cpp G5500_Fork/panel_ui.cpp G5500_Fork/panel_fonts.cpp \
  G5500_Fork/crowpanel_font_11.cpp G5500_Fork/crowpanel_font_22.cpp G5500_Fork/crowpanel_font_28.cpp \
  G5500_Fork/crowpanel_font_36.cpp G5500_Fork/crowpanel_font_80.cpp G5500_Fork/crowpanel_font_112.cpp \
  build/motion/libruckig.a "$build/lvgl/liblvgl.a" -lm -pthread -o "$build/knob-draw-tests"
"$build/knob-draw-tests" "$@"
