#pragma once
#include "../fakes/Arduino.h"
inline void configTime(long, int, const char *, const char *) {}
inline void delay(uint32_t ms) { fakeHardware.now+=ms; }
