#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
target="${1:-s3}"
case "$target" in
  s3) default_fqbn='esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=custom,USBMode=hwcdc,CDCOnBoot=cdc,FlashMode=qio' ;;
  s2) default_fqbn='esp32:esp32:esp32s2:FlashSize=4M,PSRAM=disabled,PartitionScheme=min_spiffs,CDCOnBoot=cdc,UploadMode=cdc' ;;
  *) echo 'Usage: tools/build_arduino.sh [s3|s2]' >&2; exit 2 ;;
esac
case "${DISPLAY_ENABLED:-}" in
  ''|0|1) ;;
  *) echo 'DISPLAY_ENABLED must be 0 or 1' >&2; exit 2 ;;
esac
python3 tools/prepare_sketch.py --target "$target"
mkdir -p "artifacts/arduino-$target"
set -- compile --clean --jobs "${BUILD_JOBS:-4}" \
  --fqbn "${FQBN:-$default_fqbn}" \
  --build-path "build/arduino-build-$target" \
  --output-dir "artifacts/arduino-$target" "build/arduino-$target/G5500_Fork"
if [ -n "${DISPLAY_ENABLED:-}" ]; then
  set -- "$@" --build-property "compiler.cpp.extra_flags=-DTFT_DISPLAY_ENABLED=$DISPLAY_ENABLED"
fi
if [ -n "${ARDUINO_CONFIG_FILE:-}" ]; then
  set -- --config-file "$ARDUINO_CONFIG_FILE" "$@"
fi
"${ARDUINO_CLI:-arduino-cli}" "$@"
