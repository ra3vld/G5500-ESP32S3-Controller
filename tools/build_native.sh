#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
if [ "${1:-}" = "--quick" ]; then
  shift
  exec python3 tools/check_native.py "$@"
fi
mkdir -p build
python3 tools/project_version.py
python3 -m unittest discover -s tests -p test_check_native.py
python3 tools/generate_web_assets.py --check
python3 tools/generate_tft_image.py --check
python3 -m unittest discover -s tests -p test_tft_image.py
python3 -m unittest discover -s tests -p web_assets_tests.py
python3 -m unittest discover -s tests -p test_crowpanel_rgb.py
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -DRGB_INDICATOR_TEST -I tests/rgb_fakes -I tests/fakes -I G5500_Fork tests/rgb_indicator_tests.cpp -o build/rgb_indicator_tests
./build/rgb_indicator_tests
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -I tests/ota_fakes -I G5500_Fork G5500_Fork/ota.cpp tests/ota_tests.cpp -o build/ota_tests
./build/ota_tests
sh tools/build_ruckig_native.sh
sh tools/test_st7796.sh
sh tools/test_motion_rates.sh
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -I G5500_Fork/src/ruckig tests/ruckig_workspace_tests.cpp build/motion/libruckig.a -o build/ruckig_workspace_tests
./build/ruckig_workspace_tests
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -I G5500_Fork tests/socket_write_tests.cpp -o build/socket_write_tests
./build/socket_write_tests
MOTION_SOURCES="G5500_Fork/motion.cpp build/motion/libruckig.a"
CORE_SOURCES="G5500_Fork/control.cpp $MOTION_SOURCES G5500_Fork/protocol.cpp G5500_Fork/input.cpp G5500_Fork/api.cpp G5500_Fork/live.cpp G5500_Fork/calibration.cpp G5500_Fork/hud.cpp"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -DDRIVE_SIMULATION=0 -I G5500_Fork $CORE_SOURCES tests/coast_measurement_tests.cpp -o build/coast_measurement_tests
./build/coast_measurement_tests
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -I G5500_Fork $CORE_SOURCES tests/keyboard_hold_tests.cpp -o build/keyboard_hold_tests
./build/keyboard_hold_tests
sh tools/test_local_controls.sh
sh tools/test_position_sensors.sh
sh tools/test_rs485.sh
sh tools/test_modbus.sh
sh tools/test_round_panel.sh
sh tools/test_esp32c3.sh

${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -DWEB_INTERFACE_ENABLED=0 -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/storage.cpp tests/setup_noweb_tests.cpp -o build/setup_noweb_tests
./build/setup_noweb_tests

# WT32: PHY setup, cable/DHCP states and Ethernet reservations in every drive mode.
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -DBOARD_PROFILE=6 -I tests/ethernet_fakes -I tests/device_fakes -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/hardware.cpp tests/ethernet_tests.cpp -o build/ethernet_tests
./build/ethernet_tests

# Waveshare: camera/Ethernet reservations, four native motor PWM and capture failures.
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -DBOARD_PROFILE=7 -DDRIVE_INTERFACE=2 -I tests/ethernet_fakes -I tests/device_fakes -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/hardware.cpp tests/waveshare_tests.cpp -o build/waveshare_tests
./build/waveshare_tests
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -DBOARD_PROFILE=7 -I tests/camera_fakes -I tests/fakes -I G5500_Fork G5500_Fork/camera.cpp tests/camera_tests.cpp -o build/camera_tests
./build/camera_tests

# Persisted GPIO ownership and adapters on every board, including both CrowPanel profiles.
for board in 1 2 3 4 5; do
  extra=""
  if [ "$board" = 5 ]; then extra="-DDISPLAY_KIND=3 -DUSER_BUTTON_GPIO=-1"; fi
  ${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -DBOARD_PROFILE=$board $extra -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/hardware.cpp G5500_Fork/storage.cpp tests/pin_map_tests.cpp -o build/pin_map_tests_$board
  ./build/pin_map_tests_$board
  ./build/pin_map_tests_$board --describe > build/pin-map-$board.json
done
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -DBOARD_PROFILE=5 -DDISPLAY_KIND=3 -DUSER_BUTTON_GPIO=-1 -DCROWPANEL_LOCAL_DRIVE=1 -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/hardware.cpp G5500_Fork/storage.cpp tests/pin_map_tests.cpp -o build/pin_map_tests_az
./build/pin_map_tests_az
./build/pin_map_tests_az --describe > build/pin-map-az.json

for board in 1 2 3 4; do
  for display in 1 2; do
    ${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -DPIN_MAP_DESCRIBE_ONLY -DBOARD_PROFILE=$board -DDISPLAY_KIND=$display -I G5500_Fork tests/pin_map_tests.cpp -o build/pin_map_describe
    ./build/pin_map_describe --describe > build/pin-map-$board-d$display.json
  done
done

${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -I G5500_Fork G5500_Fork/command_api.cpp tests/command_api_tests.cpp -o build/command_api_tests
./build/command_api_tests
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -DDEVICE_API_TEST -DBOARD_PROFILE=4 -DBLUETOOTH_SUPPORT=0 -I tests/device_fakes -I tests/ota_fakes -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/storage.cpp G5500_Fork/hardware.cpp G5500_Fork/ota.cpp G5500_Fork/device_api.cpp G5500_Fork/command_api.cpp tests/device_api_tests.cpp -o build/device_api_tests
./build/device_api_tests
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -DDEVICE_API_TEST -DBOARD_PROFILE=6 -DBLUETOOTH_SUPPORT=0 -I tests/ethernet_fakes -I tests/device_fakes -I tests/ota_fakes -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/storage.cpp G5500_Fork/hardware.cpp G5500_Fork/ota.cpp G5500_Fork/device_api.cpp G5500_Fork/command_api.cpp tests/device_api_tests.cpp -o build/device_api_wt32_tests
./build/device_api_wt32_tests
python3 -m unittest discover -s tests -p test_command_api.py
python3 -m unittest discover -s tests -p test_setup_transport.py
python3 -m unittest discover -s tests -p test_autotune_experiment.py
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I G5500_Fork $CORE_SOURCES tests/motion_tests.cpp -o build/motion_tests
./build/motion_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I G5500_Fork $CORE_SOURCES tests/servo_hold_tests.cpp -o build/servo_hold_tests
./build/servo_hold_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I G5500_Fork $CORE_SOURCES tests/stream_motion_tests.cpp -o build/stream_motion_tests
./build/stream_motion_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I G5500_Fork $CORE_SOURCES tests/static_target_tests.cpp -o build/static_target_tests
./build/static_target_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I G5500_Fork $CORE_SOURCES tests/autotune_tests.cpp -o build/autotune_tests
./build/autotune_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/hardware.cpp G5500_Fork/storage.cpp tests/motion_settings_tests.cpp -o build/motion_settings_tests
./build/motion_settings_tests
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/hardware.cpp G5500_Fork/storage.cpp tests/hardware_runtime_tests.cpp -o build/hardware_runtime_tests
./build/hardware_runtime_tests
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/storage.cpp tests/reset_history_tests.cpp -o build/reset_history_tests
./build/reset_history_tests
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/hardware.cpp G5500_Fork/storage.cpp tests/simulation_config_tests.cpp -o build/simulation_config_tests
./build/simulation_config_tests
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -DBOARD_PROFILE=5 -DDISPLAY_KIND=3 -DUSER_BUTTON_GPIO=-1 -DBLUETOOTH_ENABLED=0 -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/hardware.cpp G5500_Fork/storage.cpp tests/simulation_config_tests.cpp -o build/crowpanel_simulation_config_tests
./build/crowpanel_simulation_config_tests
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -DBOARD_PROFILE=8 -DDISPLAY_KIND=4 -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/hardware.cpp G5500_Fork/storage.cpp tests/simulation_config_tests.cpp -o build/round_simulation_config_tests
./build/round_simulation_config_tests
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/hardware.cpp G5500_Fork/storage.cpp tests/hud_hardware_config_tests.cpp -o build/hud_hardware_config_tests
./build/hud_hardware_config_tests
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -DBOARD_PROFILE=5 -DDISPLAY_KIND=3 -DUSER_BUTTON_GPIO=-1 -DBLUETOOTH_ENABLED=0 -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/hardware.cpp G5500_Fork/storage.cpp tests/hud_hardware_config_tests.cpp -o build/crowpanel_hud_hardware_config_tests
./build/crowpanel_hud_hardware_config_tests
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -DBOARD_PROFILE=8 -DDISPLAY_KIND=4 -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/hardware.cpp G5500_Fork/storage.cpp tests/hud_hardware_config_tests.cpp -o build/round_hud_hardware_config_tests
./build/round_hud_hardware_config_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I G5500_Fork $CORE_SOURCES tests/live_tests.cpp -o build/live_tests
./build/live_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -pthread -DARDUINO_ARCH_ESP32 -DBOARD_PROFILE=4 -I tests/live_fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/live_server.cpp tests/live_server_tests.cpp -o build/live_server_tests
./build/live_server_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I G5500_Fork $CORE_SOURCES tests/hud_latency_tests.cpp -o build/hud_latency_tests
./build/hud_latency_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I G5500_Fork $CORE_SOURCES tests/hud_live_tests.cpp -o build/hud_live_tests
./build/hud_live_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I G5500_Fork $CORE_SOURCES tests/hud_backpressure_tests.cpp -o build/hud_backpressure_tests
./build/hud_backpressure_tests
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -I G5500_Fork tests/hud_uart_gate_tests.cpp -o build/hud_uart_gate_tests
./build/hud_uart_gate_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I G5500_Fork $CORE_SOURCES tests/hud_radio_tests.cpp -o build/hud_radio_tests
./build/hud_radio_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I G5500_Fork $CORE_SOURCES tools/simulator.cpp -o build/simulator
python3 -m unittest discover -s tests -p test_control_generator.py
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I G5500_Fork $CORE_SOURCES tests/core_tests.cpp -o build/core_tests
./build/core_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I G5500_Fork G5500_Fork/input.cpp tests/selection_input_tests.cpp -o build/selection_input_tests
./build/selection_input_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I G5500_Fork $CORE_SOURCES tests/protocol_memory_tests.cpp -o build/protocol_memory_tests
./build/protocol_memory_tests
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -DDRIVE_SIMULATION=0 -I G5500_Fork G5500_Fork/control.cpp $MOTION_SOURCES G5500_Fork/protocol.cpp tests/protocol_shortest_tests.cpp -o build/protocol_shortest_tests
./build/protocol_shortest_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/storage.cpp tests/external_az_settings_tests.cpp -o build/external_az_settings_tests
./build/external_az_settings_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/storage.cpp tests/manual_protocol_tests.cpp -o build/manual_protocol_tests
./build/manual_protocol_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -pthread -I tests/http_fakes -I G5500_Fork tests/wifi_fallback_tests.cpp -o build/wifi_fallback_tests
./build/wifi_fallback_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I G5500_Fork tests/diagnostic_stream_tests.cpp -o build/diagnostic_stream_tests
./build/diagnostic_stream_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I tests/http_fakes -I tests/fakes -I G5500_Fork tests/http_ingress_tests.cpp -o build/http_ingress_tests
./build/http_ingress_tests
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -DBOARD_PROFILE=4 -I tests/wifi_fakes -I G5500_Fork tests/wifi_memory_tests.cpp -o build/wifi_memory_tests
./build/wifi_memory_tests

${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -ffunction-sections -fdata-sections -Wl,--gc-sections -DBOARD_PROFILE=8 -DDISPLAY_KIND=4 -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1 -I tests/diagnostics_fakes -I tests/device_fakes -I tests/fakes -I G5500_Fork G5500_Fork/diagnostics.cpp tests/usb_diagnostics_tests.cpp -o build/usb_diagnostics_tests
./build/usb_diagnostics_tests
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -ffunction-sections -fdata-sections -Wl,--gc-sections -DBOARD_PROFILE=1 -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1 -I tests/diagnostics_fakes -I tests/device_fakes -I tests/fakes -I G5500_Fork G5500_Fork/diagnostics.cpp tests/usb_diagnostics_tests.cpp -o build/usb_diagnostics_s3_tests
./build/usb_diagnostics_s3_tests
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -ffunction-sections -fdata-sections -Wl,--gc-sections -DBOARD_PROFILE=2 -DARDUINO_USB_MODE=0 -DARDUINO_USB_CDC_ON_BOOT=1 -I tests/diagnostics_fakes -I tests/device_fakes -I tests/fakes -I G5500_Fork G5500_Fork/diagnostics.cpp tests/usb_diagnostics_tests.cpp -o build/usb_diagnostics_s2_tests
./build/usb_diagnostics_s2_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -pthread -I G5500_Fork G5500_Fork/control.cpp $MOTION_SOURCES G5500_Fork/protocol.cpp tests/datagram_socket_tests.cpp -Wl,--wrap=malloc -o build/datagram_socket_tests
./build/datagram_socket_tests
for board in 1 4; do
  ${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -DBOARD_PROFILE=$board -I G5500_Fork $CORE_SOURCES tests/status_memory_tests.cpp -o build/status_memory_tests
  ./build/status_memory_tests
done
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/hardware.cpp G5500_Fork/storage.cpp tests/adapter_tests.cpp -o build/adapter_tests
./build/adapter_tests
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -DBOARD_PROFILE=4 -DBLUETOOTH_ENABLED=0 -I tests/fakes -I G5500_Fork G5500_Fork/bluetooth.cpp G5500_Fork/input.cpp tests/bluetooth_runtime_tests.cpp -o build/bluetooth_runtime_tests
./build/bluetooth_runtime_tests
for flags in '-DBOARD_PROFILE=4 -DBLUETOOTH_ENABLED=1' '-DBOARD_PROFILE=4 -DBLUETOOTH_ENABLED=0' '-DBOARD_PROFILE=1 -DBLUETOOTH_ENABLED=1' '-DBOARD_PROFILE=4 -DBLUETOOTH_ENABLED=1 -DBLUETOOTH_MODE=BT_HC05'; do
  ${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -pthread $flags -I tests/fakes -I G5500_Fork G5500_Fork/bluetooth.cpp G5500_Fork/input.cpp G5500_Fork/control.cpp $MOTION_SOURCES G5500_Fork/protocol.cpp tests/bluetooth_tests.cpp -o build/bluetooth_tests
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

${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I G5500_Fork G5500_Fork/control.cpp $MOTION_SOURCES tests/crowpanel_tests.cpp -o build/crowpanel_tests
./build/crowpanel_tests
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -I G5500_Fork tests/drive_fault_text_tests.cpp -o build/drive_fault_text_tests
./build/drive_fault_text_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I G5500_Fork $CORE_SOURCES tests/motion_reset_input_tests.cpp -o build/motion_reset_input_tests
./build/motion_reset_input_tests
sh tools/test_panel.sh
CROW_FLAGS="-DBOARD_PROFILE=5 -DDISPLAY_KIND=3 -DUSER_BUTTON_GPIO=-1 -DBLUETOOTH_ENABLED=0"
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 $CROW_FLAGS -DCROWPANEL_SIMULATION=0 -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/hardware.cpp tests/crowpanel_hardware_tests.cpp -o build/crowpanel_hardware_tests
./build/crowpanel_hardware_tests
for mode in yaesu bts inverted; do
  flags="-DCROWPANEL_LOCAL_DRIVE=1 -DDRIVE_INTERFACE=0"
  if [ "$mode" = bts ]; then flags="-DCROWPANEL_LOCAL_DRIVE=1 -DDRIVE_INTERFACE=2"; fi
  if [ "$mode" = inverted ]; then flags="-DCROWPANEL_LOCAL_DRIVE=1 -DDRIVE_INTERFACE=2 -DBTS7960_PWM_INVERTED=1"; fi
  ${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 $CROW_FLAGS $flags -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/hardware.cpp G5500_Fork/storage.cpp tests/crowpanel_drive_tests.cpp -o build/crowpanel_drive_$mode
  ./build/crowpanel_drive_$mode
done
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
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I G5500_Fork $CORE_SOURCES tests/hud_simulation_tests.cpp -o build/hud_simulation_tests
./build/hud_simulation_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I G5500_Fork $CORE_SOURCES tests/hud_position_json_tests.cpp -o build/hud_position_json_tests
./build/hud_position_json_tests
for drive in 0 1 2; do
  ${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -DDRIVE_INTERFACE=$drive -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/hardware.cpp G5500_Fork/storage.cpp tests/hud_adapter_tests.cpp -o build/hud_adapter_tests_$drive
  ./build/hud_adapter_tests_$drive
done
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 -I G5500_Fork $CORE_SOURCES tests/hud_memory_tests.cpp -o build/hud_memory_tests
./build/hud_memory_tests
${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=0 -Wall -Wextra -Werror -O2 $CROW_FLAGS -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/hardware.cpp G5500_Fork/storage.cpp tests/hud_adapter_tests.cpp -o build/hud_crowpanel_tests
./build/hud_crowpanel_tests

for simulation in 0 1; do
  ${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -DDRIVE_SIMULATION=$simulation -I G5500_Fork $CORE_SOURCES tests/az_range_tests.cpp -o build/az_range_tests
  ./build/az_range_tests
done
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -DDRIVE_SIMULATION=0 -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/storage.cpp tests/az_range_storage_tests.cpp -o build/az_range_storage_tests
./build/az_range_storage_tests
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -DDRIVE_SIMULATION=0 -I G5500_Fork $CORE_SOURCES tests/az_range_hud_tests.cpp -o build/az_range_hud_tests
./build/az_range_hud_tests

for simulation in 0 1; do
  ${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -DDRIVE_SIMULATION=$simulation -I G5500_Fork $CORE_SOURCES tests/el_range_tests.cpp -o build/el_range_tests
  ./build/el_range_tests
done

for variant in 0:0 1:0 2:0 2:1; do
  drive=${variant%:*}; simulation=${variant#*:}
  ${CXX:-g++} -std=c++17 -DDRIVE_SIMULATION=$simulation -DDRIVE_INTERFACE=$drive -Wall -Wextra -Werror -O2 -I tests/fakes -I G5500_Fork $CORE_SOURCES G5500_Fork/hardware.cpp G5500_Fork/storage.cpp tests/coast_settings_tests.cpp -o build/coast_settings_tests
  ./build/coast_settings_tests
done
