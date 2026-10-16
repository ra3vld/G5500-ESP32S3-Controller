#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
: "${LVGL_DIR:?Set LVGL_DIR to the lvgl 9.6.0 source directory}"
cmake_bin="${CMAKE:-cmake}"
root="$(pwd)"
build="$root/build/crowpanel-preview"
"$cmake_bin" -S "$LVGL_DIR" -B "$build/lvgl" -DLV_BUILD_CONF_PATH="$root/G5500_Fork/lv_conf.h" -DCONFIG_LV_BUILD_EXAMPLES=OFF -DCONFIG_LV_BUILD_DEMOS=OFF -DCMAKE_BUILD_TYPE=Release
"$cmake_bin" --build "$build/lvgl" -j "${BUILD_JOBS:-4}"
${CXX:-g++} -std=c++17 -O2 -DBOARD_PROFILE=5 -DDISPLAY_KIND=3 -DUSER_BUTTON_GPIO=-1 -DBLUETOOTH_ENABLED=0 -DLV_CONF_INCLUDE_SIMPLE \
  -I G5500_Fork -I "$LVGL_DIR" tools/crowpanel_preview.cpp G5500_Fork/crowpanel_ui.cpp \
  G5500_Fork/crowpanel_font_11.cpp G5500_Fork/crowpanel_font_22.cpp G5500_Fork/crowpanel_font_28.cpp G5500_Fork/crowpanel_font_96.cpp \
  "$build/lvgl/liblvgl.a" -lm -pthread -o "$build/preview"
"$build/preview" "$build"
mkdir -p "$build/direct"
"$build/preview" "$build/direct" direct
