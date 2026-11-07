#pragma once
#include "FreeRTOS.h"
inline int xTaskCreate(void (*run)(void *),const char *,unsigned,void *context,int,TaskHandle_t *handle){
  if(fakeMqttTaskFailure)return 0;
  auto task=std::make_unique<FakeMqttTask>();*handle=task.get();
  task->thread=std::thread([run,context,task=task.get()]{fakeCurrentTask=task;run(context);});
  fakeMqttTasks.push_back(std::move(task));return pdPASS;
}
inline int xTaskCreatePinnedToCore(void (*run)(void *),const char *name,unsigned stack,void *context,int priority,TaskHandle_t *handle,int){
  return xTaskCreate(run,name,stack,context,priority,handle);
}
inline void xTaskNotifyGive(TaskHandle_t task){
  std::lock_guard<std::mutex> lock(task->mutex);task->notified=true;task->changed.notify_one();
}
inline void ulTaskNotifyTake(int,unsigned timeout){
  auto &task=*fakeCurrentTask;std::unique_lock<std::mutex> lock(task.mutex);
  task.changed.wait_for(lock,std::chrono::milliseconds(timeout),[&]{return task.notified;});task.notified=false;
}
inline void vTaskDelay(unsigned ticks){std::this_thread::sleep_for(std::chrono::milliseconds(ticks));}
inline void vTaskDelete(void *){}
inline uint32_t uxTaskGetStackHighWaterMark(void *){return 2048;}
