#pragma once
#include <cstdint>
using BaseType_t=int;
using UBaseType_t=unsigned;
using TaskHandle_t=void *;
using TaskFunction_t=void (*)(void *);
constexpr BaseType_t tskNO_AFFINITY=-1;
