#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build
sh tools/build_ruckig_native.sh
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -DDRIVE_INTERFACE=0 -I G5500_Fork \
  G5500_Fork/control.cpp G5500_Fork/motion.cpp build/motion/libruckig.a G5500_Fork/protocol.cpp G5500_Fork/hud.cpp \
  tests/crowpanel_touch_tests.cpp -o build/crowpanel_touch_tests_0
./build/crowpanel_touch_tests_0
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -DDRIVE_INTERFACE=1 -I G5500_Fork \
  G5500_Fork/control.cpp G5500_Fork/motion.cpp build/motion/libruckig.a G5500_Fork/protocol.cpp G5500_Fork/hud.cpp \
  tests/crowpanel_touch_tests.cpp -o build/crowpanel_touch_tests_1
./build/crowpanel_touch_tests_1
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -DDRIVE_INTERFACE=2 -I G5500_Fork \
  G5500_Fork/control.cpp G5500_Fork/motion.cpp build/motion/libruckig.a G5500_Fork/protocol.cpp G5500_Fork/hud.cpp \
  tests/crowpanel_touch_tests.cpp -o build/crowpanel_touch_tests_2
./build/crowpanel_touch_tests_2
