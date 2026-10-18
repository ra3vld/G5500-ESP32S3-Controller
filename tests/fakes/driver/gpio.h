#pragma once
#include <Arduino.h>
using gpio_num_t = int;
inline constexpr int GPIO_MODE_INPUT_OUTPUT_OD = 7;
inline int gpio_set_direction(gpio_num_t pin, int mode) {
  fakeHardware.events.push_back({"direction",pin,unsigned(mode)}); return 0;
}
inline int gpio_set_level(gpio_num_t pin, unsigned level) {
  fakeHardware.levels[pin]=int(level);
  fakeHardware.events.push_back({"preload",pin,level});
  return 0;
}
