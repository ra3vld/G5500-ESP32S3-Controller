#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -I G5500_Fork tests/gt911_tests.cpp -o build/gt911_tests
./build/gt911_tests
sh tools/build_ruckig_native.sh
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -DBOARD_PROFILE=10 -DDISPLAY_KIND=5 -I G5500_Fork \
  G5500_Fork/control.cpp G5500_Fork/motion.cpp build/motion/libruckig.a G5500_Fork/protocol.cpp G5500_Fork/hud.cpp \
  tests/st7796_board_tests.cpp -o build/st7796_board_tests
./build/st7796_board_tests
