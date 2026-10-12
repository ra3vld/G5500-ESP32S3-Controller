#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build
CORE_SOURCES="G5500_Fork/control.cpp G5500_Fork/protocol.cpp G5500_Fork/input.cpp G5500_Fork/api.cpp G5500_Fork/calibration.cpp"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -I G5500_Fork $CORE_SOURCES tools/simulator.cpp -o build/simulator
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -I G5500_Fork $CORE_SOURCES tests/core_tests.cpp -o build/core_tests
./build/core_tests
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/hardware.cpp G5500_Fork/storage.cpp tests/adapter_tests.cpp -o build/adapter_tests
./build/adapter_tests
