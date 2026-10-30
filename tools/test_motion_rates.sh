#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
sh tools/build_ruckig_native.sh
${CXX:-g++} -std=c++17 -O2 -Wall -Wextra -Werror -DDRIVE_SIMULATION=0 \
  -DMOTION_CONTROL_PERIOD_MS=20 -I G5500_Fork G5500_Fork/control.cpp \
  G5500_Fork/motion.cpp G5500_Fork/protocol.cpp G5500_Fork/input.cpp \
  tests/motion_rate_tests.cpp build/motion/libruckig.a -o build/motion_rate_20
./build/motion_rate_20
${CXX:-g++} -std=c++17 -O2 -Wall -Wextra -Werror -DDRIVE_SIMULATION=0 \
  -DMOTION_CONTROL_PERIOD_MS=10 -I G5500_Fork G5500_Fork/control.cpp \
  G5500_Fork/motion.cpp G5500_Fork/protocol.cpp G5500_Fork/input.cpp \
  tests/motion_rate_tests.cpp build/motion/libruckig.a -o build/motion_rate_10
./build/motion_rate_10
${CXX:-g++} -std=c++17 -O2 -Wall -Wextra -Werror -DDRIVE_SIMULATION=0 \
  -DMOTION_CONTROL_PERIOD_MS=5 -I G5500_Fork G5500_Fork/control.cpp \
  G5500_Fork/motion.cpp G5500_Fork/protocol.cpp G5500_Fork/input.cpp \
  tests/motion_rate_tests.cpp build/motion/libruckig.a -o build/motion_rate_5
./build/motion_rate_5
