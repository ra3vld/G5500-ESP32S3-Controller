#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -I G5500_Fork G5500_Fork/control.cpp G5500_Fork/protocol.cpp G5500_Fork/api.cpp tools/simulator.cpp -o build/simulator
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -I G5500_Fork G5500_Fork/control.cpp G5500_Fork/protocol.cpp G5500_Fork/api.cpp tests/core_tests.cpp -o build/core_tests
./build/core_tests
