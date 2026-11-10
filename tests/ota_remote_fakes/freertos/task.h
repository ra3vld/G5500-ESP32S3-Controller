#pragma once
#include "FreeRTOS.h"
inline int xTaskCreate(void(*f)(void *),const char *,unsigned,void *p,int,void *) {
  if(otaTaskFailure)return 0;
  otaWorkers.emplace_back(f,p);return pdPASS;
}
inline void vTaskDelay(unsigned n){std::this_thread::sleep_for(std::chrono::milliseconds(n));}
inline void vTaskDelete(void *){}
