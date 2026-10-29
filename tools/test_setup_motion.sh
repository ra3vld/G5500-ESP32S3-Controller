#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build
sh tools/build_ruckig_native.sh
SETUP_SOURCES="G5500_Fork/control.cpp G5500_Fork/motion.cpp build/motion/libruckig.a G5500_Fork/protocol.cpp G5500_Fork/input.cpp G5500_Fork/api.cpp G5500_Fork/live.cpp G5500_Fork/calibration.cpp G5500_Fork/hud.cpp G5500_Fork/storage.cpp G5500_Fork/hardware.cpp G5500_Fork/ota.cpp G5500_Fork/mqtt_bridge.cpp G5500_Fork/mqtt_service.cpp G5500_Fork/device_api.cpp G5500_Fork/command_api.cpp"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -DDEVICE_API_TEST -DBOARD_PROFILE=4 -DBLUETOOTH_SUPPORT=0 -I tests/device_fakes -I tests/ota_fakes -I tests/fakes -I G5500_Fork $SETUP_SOURCES tests/setup_motion_tests.cpp -o build/setup_motion_tests
./build/setup_motion_tests
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -DDEVICE_API_TEST -DBOARD_PROFILE=10 -DDISPLAY_KIND=5 -DBLUETOOTH_SUPPORT=0 -I tests/device_fakes -I tests/ota_fakes -I tests/fakes -I G5500_Fork $SETUP_SOURCES tests/setup_motion_tests.cpp -o build/st7796_setup_motion_tests
./build/st7796_setup_motion_tests
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -DDEVICE_API_TEST -DBOARD_PROFILE=4 -DBLUETOOTH_SUPPORT=0 -I tests/device_fakes -I tests/ota_fakes -I tests/fakes -I G5500_Fork $SETUP_SOURCES tests/setup_passive_tests.cpp -o build/setup_passive_tests
./build/setup_passive_tests
