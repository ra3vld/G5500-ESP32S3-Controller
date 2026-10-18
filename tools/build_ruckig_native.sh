#!/bin/sh
# Local, pinned motion kernel. No downloads or cloud client. GPL-3.0 wrapper.
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/motion
for source in G5500_Fork/src/ruckig/*.cpp; do
  object="build/motion/$(basename "$source" .cpp).o"
  if [ ! -f "$object" ] || [ "$source" -nt "$object" ] || [ -n "$(find G5500_Fork/src/ruckig -name '*.hpp' -newer "$object" -print -quit)" ]; then
    ${CXX:-g++} -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-array-bounds -c "$source" -o "$object"
  fi
done
${AR:-ar} rcs build/motion/libruckig.a build/motion/brake.o build/motion/position_*.o build/motion/velocity_*.o
