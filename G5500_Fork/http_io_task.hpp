// G5500 Fork; GPL-3.0.
#pragma once
#include "build_config.hpp"
#include "http_diagnostics.hpp"
#include "json_writer.hpp"
#if defined(ARDUINO) && defined(ARDUINO_ARCH_ESP32) && !CONFIG_FREERTOS_UNICORE && (FORK_CLASSIC_ESP32 || FORK_S3)
#include "bounded_web_server.hpp"
#include "network_snapshot.hpp"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
namespace forkrot {
// Socket/parser work runs on CPU0. All API callbacks run on the existing
// CPU1 owner, which never waits for a network send or takes an I/O mutex.
class HttpIoTask {
  static constexpr unsigned StackBytes=4096;
  alignas(16) StackType_t stack_[StackBytes/sizeof(StackType_t)];
  StaticTask_t taskStorage_{};
  TaskHandle_t task_=nullptr,owner_=nullptr;
  BoundedWebServer *server_=nullptr;
  OwnerCall call_;
  portMUX_TYPE snapshotLock_=portMUX_INITIALIZER_UNLOCKED;
  HttpDiagnostics diagnostics_;
  std::atomic<uint32_t> stackFree_{0};
  std::atomic<uint32_t> ownerWaitMaxUs_{0};
  uint32_t ownerJobs_=0,ownerMaxUs_=0;
  static void execute(void *context,OwnerCall::Function function,void *arguments) {
    auto &self=*static_cast<HttpIoTask *>(context);
    const auto started=micros();
    const bool posted=self.call_.post(function,arguments);
    configASSERT(posted);
    xTaskNotifyGive(self.owner_);
    while(!self.call_.takeResult())ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
    self.ownerWaitMaxUs_.store(std::max(self.ownerWaitMaxUs_.load(),uint32_t(micros()-started)));
  }
  static void worker(void *context) {
    auto &self=*static_cast<HttpIoTask *>(context);
    uint32_t radioAt=millis()-200;
    for(;;) {
      const uint32_t now=millis();
      if(uint32_t(now-radioAt)>=200) {
        publishNetworkSnapshot();radioAt=now;
        // The watermark persists after a deep call returns. Scanning the
        // entire stack once per millisecond wastes CPU0's network headroom.
        self.stackFree_.store(uxTaskGetStackHighWaterMark(nullptr),std::memory_order_relaxed);
      }
      self.server_->handleClient();
      portENTER_CRITICAL(&self.snapshotLock_);
      self.diagnostics_=self.server_->diagnostics();
      portEXIT_CRITICAL(&self.snapshotLock_);
      vTaskDelay(pdMS_TO_TICKS(self.server_->pollIntervalMs()));
    }
  }
public:
  bool begin(BoundedWebServer &server) {
    if(task_)return true;
    owner_=xTaskGetCurrentTaskHandle();server_=&server;
    server.setExecutor(execute,this);
    setNetworkSnapshotBackground(true);
    task_=xTaskCreateStaticPinnedToCore(worker,"http-io",StackBytes,this,1,stack_,&taskStorage_,0);
    if(!task_) { setNetworkSnapshotBackground(false);server.setExecutor(nullptr,nullptr);return false; }
    // Preserve one state owner and the existing 8 KiB stack. Adding another
    // motor task plus a duplicate Controller would waste scarce internal RAM.
    vTaskPrioritySet(owner_,4);
    return true;
  }
  bool active() const {return task_!=nullptr;}
  void service() {
    if(!task_)return;
    const uint32_t started=micros();
    if(call_.service()) {
      ++ownerJobs_;ownerMaxUs_=std::max(ownerMaxUs_,uint32_t(micros()-started));
      xTaskNotifyGive(task_);
    }
  }
  void wait(unsigned ms) {
    if(task_ && ms)ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(ms));
    else delay(ms);
  }
  HttpDiagnostics diagnostics() {
    portENTER_CRITICAL(&snapshotLock_);
    const auto result=diagnostics_;
    portEXIT_CRITICAL(&snapshotLock_);
    return result;
  }
  void writeDiagnostics(JsonWriter &out) {
    out<<",\"httpIoTask\":"<<(active()?"true":"false")
       <<",\"controlCore\":"<<xPortGetCoreID()<<",\"controlPriority\":"<<uxTaskPriorityGet(nullptr)
       <<",\"controlStackFree\":"<<uxTaskGetStackHighWaterMark(nullptr)
       <<",\"httpIoCore\":0,\"httpIoStackFree\":"<<stackFree_.load(std::memory_order_relaxed)
       <<",\"httpOwnerJobs\":"<<ownerJobs_<<",\"httpOwnerMaxUs\":"<<ownerMaxUs_
       <<",\"httpOwnerWaitMaxUs\":"<<ownerWaitMaxUs_.load();
  }
};
}
#else
namespace forkrot {
class HttpIoTask {
public:
  template<class Server> bool begin(Server &) {return false;}
  bool active() const {return false;}
  void service() {}
  void wait(unsigned ms) {delay(ms);}
  void writeDiagnostics(JsonWriter &) {}
};
}
#endif
