#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build
python3 tools/generate_web_assets.py --check
python3 -m unittest discover -s tests -p web_assets_tests.py
python3 -m unittest discover -s tests -p test_crowpanel_rgb.py
CORE_SOURCES="G5500_Fork/control.cpp G5500_Fork/protocol.cpp G5500_Fork/input.cpp G5500_Fork/api.cpp G5500_Fork/live.cpp G5500_Fork/calibration.cpp G5500_Fork/hud.cpp"
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I G5500_Fork $CORE_SOURCES tests/live_tests.cpp -o build/live_tests
./build/live_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -pthread -DARDUINO_ARCH_ESP32 -DBOARD_PROFILE=4 -I tests/live_fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/live_server.cpp tests/live_server_tests.cpp -o build/live_server_tests
./build/live_server_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I G5500_Fork $CORE_SOURCES tests/hud_latency_tests.cpp -o build/hud_latency_tests
./build/hud_latency_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I G5500_Fork $CORE_SOURCES tests/hud_live_tests.cpp -o build/hud_live_tests
./build/hud_live_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I G5500_Fork $CORE_SOURCES tools/simulator.cpp -o build/simulator
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I G5500_Fork $CORE_SOURCES tests/core_tests.cpp -o build/core_tests
./build/core_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I G5500_Fork G5500_Fork/input.cpp tests/selection_input_tests.cpp -o build/selection_input_tests
./build/selection_input_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I G5500_Fork $CORE_SOURCES tests/protocol_memory_tests.cpp -o build/protocol_memory_tests
./build/protocol_memory_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -pthread -I tests/http_fakes -I G5500_Fork tests/wifi_fallback_tests.cpp -o build/wifi_fallback_tests
./build/wifi_fallback_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I G5500_Fork tests/diagnostic_stream_tests.cpp -o build/diagnostic_stream_tests
./build/diagnostic_stream_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I tests/http_fakes -I tests/fakes -I G5500_Fork tests/http_ingress_tests.cpp -o build/http_ingress_tests
./build/http_ingress_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -pthread -I G5500_Fork G5500_Fork/control.cpp G5500_Fork/protocol.cpp tests/datagram_socket_tests.cpp -Wl,--wrap=malloc -o build/datagram_socket_tests
./build/datagram_socket_tests
for board in 1 4; do
  ${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -DBOARD_PROFILE=$board -I G5500_Fork $CORE_SOURCES tests/status_memory_tests.cpp -o build/status_memory_tests
  ./build/status_memory_tests
done
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/hardware.cpp G5500_Fork/storage.cpp tests/adapter_tests.cpp -o build/adapter_tests
./build/adapter_tests
for flags in '-DBOARD_PROFILE=4 -DBLUETOOTH_ENABLED=1' '-DBOARD_PROFILE=4 -DBLUETOOTH_ENABLED=0' '-DBOARD_PROFILE=1 -DBLUETOOTH_ENABLED=1' '-DBOARD_PROFILE=4 -DBLUETOOTH_ENABLED=1 -DBLUETOOTH_MODE=BT_HC05'; do
  ${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -pthread $flags -I tests/fakes -I G5500_Fork G5500_Fork/bluetooth.cpp G5500_Fork/input.cpp G5500_Fork/control.cpp G5500_Fork/protocol.cpp tests/bluetooth_tests.cpp -o build/bluetooth_tests
  ./build/bluetooth_tests
  ${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 $flags -I G5500_Fork $CORE_SOURCES tests/build_metadata_tests.cpp -o build/build_metadata_tests
  ./build/build_metadata_tests
done
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -DTFT_DISPLAY_ENABLED=0 -I G5500_Fork -c G5500_Fork/display.cpp -o build/display_disabled.o
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -DTFT_DISPLAY_ENABLED=0 -I G5500_Fork -c tests/display_disabled_compile.cpp -o build/display_disabled_compile.o
for drive in 0 1 2; do
  ${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -DDRIVE_INTERFACE=$drive -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/hardware.cpp G5500_Fork/storage.cpp tests/drive_tests.cpp -o build/drive_tests_$drive
  ./build/drive_tests_$drive
done
# The optocoupler wiring reverses only RPWM/LPWM, never ENABLE.
for profile in 1 3; do
  ${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -DDRIVE_INTERFACE=2 -DBTS7960_PWM_INVERTED=1 -DBOARD_PROFILE=$profile -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/hardware.cpp G5500_Fork/storage.cpp tests/drive_tests.cpp -o build/drive_tests_inverted_$profile
  ./build/drive_tests_inverted_$profile
done
for button in '-DUSER_BUTTON_GPIO=38' '-DUSER_BUTTON_GPIO=38 -DUSER_BUTTON_ACTIVE_LOW=0 -DUSER_BUTTON_PULLUP=0' '-DUSER_BUTTON_GPIO=-1'; do
  ${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 $button -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/hardware.cpp G5500_Fork/storage.cpp tests/drive_tests.cpp -o build/button_tests
  ./build/button_tests
done

${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I G5500_Fork G5500_Fork/control.cpp tests/crowpanel_tests.cpp -o build/crowpanel_tests
./build/crowpanel_tests
for drive in 0 1 2; do
  ${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -DDRIVE_INTERFACE=$drive -I G5500_Fork G5500_Fork/control.cpp G5500_Fork/protocol.cpp G5500_Fork/hud.cpp tests/crowpanel_touch_tests.cpp -o build/crowpanel_touch_tests_$drive
  ./build/crowpanel_touch_tests_$drive
done
CROW_FLAGS="-DBOARD_PROFILE=5 -DDISPLAY_KIND=3 -DUSER_BUTTON_GPIO=-1 -DBLUETOOTH_ENABLED=0"
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 $CROW_FLAGS -DCROWPANEL_SIMULATION=0 -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/hardware.cpp tests/crowpanel_hardware_tests.cpp -o build/crowpanel_hardware_tests
./build/crowpanel_hardware_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=1 -Wall -Wextra -Werror -O2 $CROW_FLAGS -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/hardware.cpp G5500_Fork/storage.cpp tests/crowpanel_simulation_tests.cpp -o build/crowpanel_simulation_tests
./build/crowpanel_simulation_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 $CROW_FLAGS -I G5500_Fork $CORE_SOURCES tests/build_metadata_tests.cpp -o build/crowpanel_metadata_tests
./build/crowpanel_metadata_tests

${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=1 -Wall -Wextra -Werror -O2 $CROW_FLAGS -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/hardware.cpp G5500_Fork/storage.cpp tests/simulation_dynamics_tests.cpp -o build/simulation_dynamics_tests
./build/simulation_dynamics_tests

for board in 1 2 3 4 5; do
  drives="0 1 2"
  extra=""
  if [ "$board" = 2 ]; then drives="0"; fi
  if [ "$board" = 5 ]; then drives="0"; extra="-DDISPLAY_KIND=3 -DUSER_BUTTON_GPIO=-1"; fi
  for drive in $drives; do
    ${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -DBOARD_PROFILE=$board -DDRIVE_INTERFACE=$drive -DDRIVE_SIMULATION=1 $extra -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/hardware.cpp tests/simulation_tests.cpp -o build/simulation_tests
    ./build/simulation_tests
  done
done

${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I G5500_Fork $CORE_SOURCES tests/hud_tests.cpp -o build/hud_tests
./build/hud_tests
for drive in 0 1 2; do
  ${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -DDRIVE_INTERFACE=$drive -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/hardware.cpp G5500_Fork/storage.cpp tests/hud_adapter_tests.cpp -o build/hud_adapter_tests_$drive
  ./build/hud_adapter_tests_$drive
done
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I G5500_Fork $CORE_SOURCES tests/hud_memory_tests.cpp -o build/hud_memory_tests
./build/hud_memory_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 $CROW_FLAGS -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/hardware.cpp G5500_Fork/storage.cpp tests/hud_adapter_tests.cpp -o build/hud_crowpanel_tests
./build/hud_crowpanel_tests

for variant in 0:0 1:0 2:0 2:1; do
  drive=${variant%:*}; simulation=${variant#*:}
  ${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=$simulation -DDRIVE_INTERFACE=$drive -Wall -Wextra -Werror -O2 -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/hardware.cpp G5500_Fork/storage.cpp tests/coast_settings_tests.cpp -o build/coast_settings_tests
  ./build/coast_settings_tests
done
