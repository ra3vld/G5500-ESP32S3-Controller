#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build
sh tools/build_ruckig_native.sh
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -DBOARD_PROFILE=4 -I tests/fakes -I G5500_Fork \
 G5500_Fork/control.cpp G5500_Fork/motion.cpp build/motion/libruckig.a G5500_Fork/protocol.cpp \
 G5500_Fork/input.cpp G5500_Fork/api.cpp G5500_Fork/live.cpp G5500_Fork/calibration.cpp G5500_Fork/hud.cpp \
 G5500_Fork/hardware.cpp G5500_Fork/storage.cpp G5500_Fork/command_api.cpp tests/position_sensor_tests.cpp \
 -o build/position_sensor_tests
./build/position_sensor_tests
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -I tests/http_fakes -I tests/fakes -I G5500_Fork \
 tests/hardware_gzip_tests.cpp -o build/hardware_gzip_tests
./build/hardware_gzip_tests
