// G5500 Fork; GPL-3.0.
#include "mqtt_crypto_budget.hpp"
#if defined(G5500_MQTT_CRYPTO_BUDGET) && G5500_MQTT_CRYPTO_BUDGET
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <cstring>
namespace forkrot {
static std::atomic<void *> creatingTlsClient{nullptr};
void pinMqttTlsClient(void *client) {creatingTlsClient.store(client);}
void beginMqttCryptoBudget() {
  mqttCryptoBudget.begin(reinterpret_cast<uintptr_t>(xTaskGetCurrentTaskHandle()),uint32_t(esp_timer_get_time()));
}
}
extern "C" BaseType_t __real_xTaskCreatePinnedToCore(TaskFunction_t,const char *const,uint32_t,void *const,UBaseType_t,TaskHandle_t *const,BaseType_t);
extern "C" BaseType_t __wrap_xTaskCreatePinnedToCore(TaskFunction_t run,const char *const name,uint32_t stack,void *const context,UBaseType_t priority,TaskHandle_t *const handle,BaseType_t core) {
  // The prebuilt MQTT SDK has no per-client affinity setting. Restrict this
  // override to its synchronous creation of our TLS task; all other tasks keep
  // their requested affinity, stack and priority. CPU1 owns Controller.
  if(context && context==forkrot::creatingTlsClient.load() && name && !std::strcmp(name,"mqtt_task") && core==tskNO_AFFINITY)core=0;
  return __real_xTaskCreatePinnedToCore(run,name,stack,context,priority,handle,core);
}
extern "C" void __real_esp_crypto_mpi_lock_release();
extern "C" void __wrap_esp_crypto_mpi_lock_release() {
  // Never sleep while holding the accelerator: TLS has exclusive ownership of
  // its own MPI values, but other crypto clients must be free to use hardware.
  __real_esp_crypto_mpi_lock_release();
  forkrot::mqttCryptoBudget.released(reinterpret_cast<uintptr_t>(xTaskGetCurrentTaskHandle()),
    [] {return uint32_t(esp_timer_get_time());},[] {vTaskDelay(1);});
}
#endif
