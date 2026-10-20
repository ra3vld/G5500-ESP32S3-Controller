#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build
sh tools/build_ruckig_native.sh
for flags in \
  '-DBOARD_PROFILE=1' \
  '-DBOARD_PROFILE=1 -DLOCAL_INPUT_MODE=1 -DLOCAL_AZ_NEG_GPIO=38 -DLOCAL_AZ_POS_GPIO=39 -DLOCAL_EL_NEG_GPIO=40 -DLOCAL_EL_POS_GPIO=41' \
  '-DBOARD_PROFILE=1 -DLOCAL_INPUT_MODE=2 -DLOCAL_AZ_ADC_GPIO=1 -DLOCAL_EL_ADC_GPIO=6' \
  '-DBOARD_PROFILE=4 -DLOCAL_INPUT_MODE=2 -DLOCAL_AZ_ADC_GPIO=36 -DLOCAL_EL_ADC_GPIO=39 -DLOCAL_AZ_INVERT=1 -DLOCAL_EL_INVERT=1'; do
  ${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 $flags -I tests/fakes -I G5500_Fork \
    G5500_Fork/control.cpp G5500_Fork/motion.cpp G5500_Fork/protocol.cpp G5500_Fork/input.cpp \
    G5500_Fork/hud.cpp G5500_Fork/hardware.cpp tests/local_input_tests.cpp \
    build/motion/libruckig.a -o build/local_input_tests
  ./build/local_input_tests
done
