#pragma once
#include "../fakes/Arduino.h"
inline bool fakePsram=true;
inline bool psramFound() { return fakePsram; }
inline void delay(uint32_t) {}
