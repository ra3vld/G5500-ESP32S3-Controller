#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p artifacts/arduino
${ARDUINO_CLI:-arduino-cli} compile --clean \
  --fqbn "${FQBN:-esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=custom,USBMode=hwcdc,CDCOnBoot=cdc,FlashMode=qio}" \
  --output-dir artifacts/arduino G5500_Fork
