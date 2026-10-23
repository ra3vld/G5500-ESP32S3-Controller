#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
sh tools/build_ruckig_native.sh
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -DDRIVE_SIMULATION=0 -I G5500_Fork G5500_Fork/control.cpp G5500_Fork/motion.cpp build/motion/libruckig.a G5500_Fork/protocol.cpp G5500_Fork/input.cpp G5500_Fork/api.cpp G5500_Fork/live.cpp G5500_Fork/calibration.cpp G5500_Fork/hud.cpp tests/modbus_tests.cpp -o build/modbus_tests
./build/modbus_tests
