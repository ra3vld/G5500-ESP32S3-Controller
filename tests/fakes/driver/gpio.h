#pragma once
#include <Arduino.h>
using gpio_num_t = int;
inline int gpio_set_level(gpio_num_t pin, unsigned level) {
  fakeHardware.levels[pin]=int(level);
  fakeHardware.events.push_back({"preload",pin,level});
  return 0;
}
