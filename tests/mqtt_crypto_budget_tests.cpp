#include "mqtt_crypto_budget.hpp"
#include <cassert>
#include <cstdio>
#include "../G5500_Fork/mqtt_crypto_budget.cpp"
using namespace forkrot;
static uint32_t fakeNow=0,delayCount=0;
static uintptr_t currentTask=7;
static bool mpiLocked=false;
static BaseType_t actualCore=0;
TaskHandle_t xTaskGetCurrentTaskHandle(){return reinterpret_cast<void *>(currentTask);}
int64_t esp_timer_get_time(){return fakeNow;}
void vTaskDelay(unsigned ticks){assert(!mpiLocked && ticks==1);++delayCount;fakeNow+=1000;}
extern "C" void __real_esp_crypto_mpi_lock_release(){assert(mpiLocked);mpiLocked=false;}
static void task(void *){}
static int client;
extern "C" BaseType_t __real_xTaskCreatePinnedToCore(TaskFunction_t run,const char *const,uint32_t stack,void *const,UBaseType_t priority,TaskHandle_t *const handle,BaseType_t core) {
  assert(run==task && stack==6144 && priority==1 && !handle);actualCore=core;return 123;
}
int main() {
  MqttCryptoBudget b;uint32_t now=10,pauses=0;
  auto clock=[&]{return now;};
  auto pause=[&]{++pauses;now+=1000;};
  b.begin(7,now);
  now+=2500;b.released(8,clock,pause);assert(!pauses && !b.checkpoints);
  b.released(7,clock,pause);assert(pauses==1 && b.maxGapUs==2500);
  now+=1999;b.released(7,clock,pause);assert(pauses==1);
  ++now;b.released(7,clock,pause);assert(pauses==2);
  b.end();now+=10000;b.released(7,clock,pause);assert(pauses==2);
  // Each connection starts a new budget and unsigned time handles rollover.
  now=UINT32_MAX-999;b.begin(9,now);now+=2000;
  b.released(9,clock,pause);assert(pauses==3);
  b.released(7,clock,pause);assert(pauses==3);
  assert(b.checkpoints==4 && b.yields==3);
  // Exercise the production SDK wrappers: the real accelerator release must
  // run before sleeping, including passthrough calls from unrelated tasks.
  beginMqttCryptoBudget();fakeNow+=2000;mpiLocked=true;
  __wrap_esp_crypto_mpi_lock_release();assert(!mpiLocked && delayCount==1);
  currentTask=8;fakeNow+=2000;mpiLocked=true;
  __wrap_esp_crypto_mpi_lock_release();assert(!mpiLocked && delayCount==1);
  currentTask=7;endMqttCryptoBudget();mpiLocked=true;
  __wrap_esp_crypto_mpi_lock_release();assert(!mpiLocked && delayCount==1);
  auto create=[&](void *context,const char *name,int core) {
    assert(__wrap_xTaskCreatePinnedToCore(task,name,6144,context,1,nullptr,core)==123);
  };
  create(&client,"mqtt_task",-1);assert(actualCore==-1);
  pinMqttTlsClient(&client);
  create(&client,"mqtt_task",-1);assert(actualCore==0);
  create(&client,"mqtt_task",1);assert(actualCore==1);
  create(&client,"another",-1);assert(actualCore==-1);
  create(nullptr,"mqtt_task",-1);assert(actualCore==-1);
  int other;create(&other,"mqtt_task",-1);assert(actualCore==-1);
  pinMqttTlsClient(nullptr);create(&client,"mqtt_task",-1);assert(actualCore==-1);
  std::puts("MQTT crypto budget PASS");
}
