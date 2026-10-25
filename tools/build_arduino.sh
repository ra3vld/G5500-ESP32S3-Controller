#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
target="${1:-s3}"
case "$target" in
  s3|crowpanel|waveshare-s3-eth) default_fqbn='esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=custom,USBMode=hwcdc,CDCOnBoot=cdc,FlashMode=qio' ;;
  s2) default_fqbn='esp32:esp32:esp32s2:FlashSize=4M,PSRAM=disabled,PartitionScheme=min_spiffs,CDCOnBoot=cdc,UploadMode=cdc' ;;
  s2-mini) default_fqbn='esp32:esp32:lolin_s2_mini:CDCOnBoot=default' ;;
  esp32) default_fqbn='esp32:esp32:esp32:FlashSize=4M,PSRAM=disabled,PartitionScheme=huge_app,FlashMode=dio,FlashFreq=40,DebugLevel=none' ;;
  wt32-eth01) default_fqbn='esp32:esp32:wt32-eth01:FlashFreq=40,PartitionScheme=huge_app' ;;
  esp32-8mb) default_fqbn='esp32:esp32:esp32:FlashSize=8M,PSRAM=disabled,PartitionScheme=custom,FlashMode=dio,FlashFreq=40,DebugLevel=none' ;;
  esp32-c3) default_fqbn='esp32:esp32:esp32c3:FlashSize=4M,PartitionScheme=custom,CDCOnBoot=cdc,FlashMode=dio' ;;
  *) echo 'Usage: tools/build_arduino.sh [s3|s2|s2-mini|esp32|esp32-8mb|esp32-c3|crowpanel|wt32-eth01|waveshare-s3-eth]' >&2; exit 2 ;;
esac
case "${DISPLAY_ENABLED:-}" in
  ''|0|1) ;;
  *) echo 'DISPLAY_ENABLED must be 0 or 1' >&2; exit 2 ;;
esac
python3 tools/prepare_sketch.py --target "$target"
mkdir -p "artifacts/arduino-$target"
case "$target" in s3|crowpanel|waveshare-s3-eth) app_size=6553600 ;; esp32|wt32-eth01|esp32-c3) app_size=3145728 ;; esp32-8mb) app_size=3670016 ;; *) app_size=1966080 ;; esac
set -- compile --jobs "${BUILD_JOBS:-4}" \
  --fqbn "${FQBN:-$default_fqbn}" \
  --build-property "upload.maximum_size=$app_size" \
  --build-path "build/arduino-build-$target" \
  --output-dir "artifacts/arduino-$target" "build/arduino-$target/G5500_Fork"
extra_flags="${EXTRA_BUILD_FLAGS:-}"
if [ "$target" = esp32-c3 ]; then
  extra_flags="$extra_flags -DDISPLAY_KIND=0 -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1 -flto"
  set -- "$@" --build-property "compiler.c.extra_flags=-flto" --build-property "compiler.c.elf.extra_flags=-flto"
fi
if [ "$target" = crowpanel ]; then
  extra_flags="$extra_flags -DDISPLAY_KIND=3 -DUSER_BUTTON_GPIO=-1 -DBLUETOOTH_ENABLED=${BLUETOOTH_ENABLED:-0} -DLV_CONF_INCLUDE_SIMPLE -I$(pwd)/build/arduino-crowpanel/G5500_Fork"
  set -- "$@" --build-property "compiler.c.extra_flags=-DLV_CONF_INCLUDE_SIMPLE -I$(pwd)/build/arduino-crowpanel/G5500_Fork"
fi
if [ -n "${DISPLAY_ENABLED:-}" ]; then extra_flags="$extra_flags -DTFT_DISPLAY_ENABLED=$DISPLAY_ENABLED"; fi
if [ -n "${DISPLAY_KIND:-}" ]; then extra_flags="$extra_flags -DDISPLAY_KIND=$DISPLAY_KIND"; fi
if [ -n "${BLUETOOTH_ENABLED:-}" ]; then extra_flags="$extra_flags -DBLUETOOTH_ENABLED=$BLUETOOTH_ENABLED"; fi
if [ -n "${DRIVE_INTERFACE:-}" ]; then extra_flags="$extra_flags -DDRIVE_INTERFACE=$DRIVE_INTERFACE"; fi
case "$target" in s3) profile=1 ;; s2) profile=2 ;; s2-mini) profile=3 ;; esp32|esp32-8mb) profile=4 ;; crowpanel) profile=5 ;; wt32-eth01) profile=6 ;; waveshare-s3-eth) profile=7 ;; esp32-c3) profile=9 ;; esac
extra_flags="$extra_flags -DBOARD_PROFILE=$profile"
case "$target" in
  s2|s2-mini)
    extra_flags="$extra_flags -flto"
    set -- "$@" --build-property "compiler.c.extra_flags=-flto" --build-property "compiler.c.elf.extra_flags=-flto"
    ;;
esac
set -- "$@" --build-property "compiler.cpp.extra_flags=$extra_flags"
if [ -n "${ARDUINO_CONFIG_FILE:-}" ]; then
  set -- --config-file "$ARDUINO_CONFIG_FILE" "$@"
fi
"${ARDUINO_CLI:-arduino-cli}" "$@"
