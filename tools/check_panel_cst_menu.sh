#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
: "${LVGL_DIR:?Set LVGL_DIR}"
build="${PREVIEW_BUILD_DIR:-build/unified-panel-preview}"
sh tools/build_ruckig_native.sh
${CXX:-g++} -std=c++17 -O2 -ffunction-sections -fdata-sections -DDEVICE_API_TEST -DBOARD_PROFILE=8 -DDISPLAY_KIND=4 \
  -DBLUETOOTH_SUPPORT=0 -DMQTT_SUPPORT=0 -DLV_CONF_INCLUDE_SIMPLE \
  -I tests/cst_fakes -I tests -I tests/device_fakes -I tests/ota_fakes -I tests/fakes -I G5500_Fork -I "$LVGL_DIR" \
  tools/panel_cst_menu_preview.cpp G5500_Fork/display_graphical.cpp G5500_Fork/display_spi.cpp \
  G5500_Fork/panel_ui.cpp G5500_Fork/panel_fonts.cpp G5500_Fork/crowpanel_font_11.cpp \
  G5500_Fork/crowpanel_font_22.cpp G5500_Fork/crowpanel_font_28.cpp G5500_Fork/crowpanel_font_36.cpp \
  G5500_Fork/crowpanel_font_80.cpp G5500_Fork/crowpanel_font_112.cpp \
  G5500_Fork/control.cpp G5500_Fork/motion.cpp G5500_Fork/protocol.cpp G5500_Fork/hud.cpp \
  G5500_Fork/calibration.cpp G5500_Fork/storage.cpp G5500_Fork/hardware.cpp G5500_Fork/ota.cpp \
  build/motion/libruckig.a "$build/lvgl/liblvgl.a" -Wl,--gc-sections -lm -pthread -o "$build/cst-menu-preview"
"$build/cst-menu-preview"
