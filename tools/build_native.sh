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
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -DTFT_DISPLAY_ENABLED=0 -I G5500_Fork -c G5500_Fork/display.cpp -o build/display_disabled.o
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -DTFT_DISPLAY_ENABLED=0 -I G5500_Fork -c tests/display_disabled_compile.cpp -o build/display_disabled_compile.o
for drive in 0 1 2; do
  ${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -DDRIVE_INTERFACE=$drive -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/hardware.cpp G5500_Fork/storage.cpp tests/drive_tests.cpp -o build/drive_tests_$drive
  ./build/drive_tests_$drive
done
# The optocoupler wiring reverses only RPWM/LPWM, never ENABLE.
for profile in 1 3; do
  ${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -DDRIVE_INTERFACE=2 -DBTS7960_PWM_INVERTED=1 -DBOARD_PROFILE=$profile -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/hardware.cpp G5500_Fork/storage.cpp tests/drive_tests.cpp -o build/drive_tests_inverted_$profile
  ./build/drive_tests_inverted_$profile
done
for button in '-DUSER_BUTTON_GPIO=38' '-DUSER_BUTTON_GPIO=38 -DUSER_BUTTON_ACTIVE_LOW=0 -DUSER_BUTTON_PULLUP=0' '-DUSER_BUTTON_GPIO=-1'; do
  ${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 $button -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/hardware.cpp G5500_Fork/storage.cpp tests/drive_tests.cpp -o build/button_tests
  ./build/button_tests
done
