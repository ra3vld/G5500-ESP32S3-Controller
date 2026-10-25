#pragma once
#include "panel_input.hpp"
#include "touch_sample.hpp"
namespace forkrot::crowpanel {
using namespace panel;
inline bool touchFrameHealthy(const uint8_t (&data)[5]) {
  return decodeCstTouch(data,480,480,0).healthy;
}
inline bool decodeTouch(const uint8_t (&data)[5], uint16_t &x, uint16_t &y) {
  const auto sample=decodeCstTouch(data,480,480,0);
  if(!sample.down) return false;
  x=sample.x;y=sample.y;return true;
}
}
