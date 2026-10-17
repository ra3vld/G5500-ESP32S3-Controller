#pragma once
#include "FreeRTOS.h"
using StaticSemaphore_t=std::mutex;
using SemaphoreHandle_t=std::mutex*;
inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *m){return m;}
inline void xSemaphoreTake(SemaphoreHandle_t m,unsigned){m->lock();}
inline void xSemaphoreGive(SemaphoreHandle_t m){m->unlock();}
