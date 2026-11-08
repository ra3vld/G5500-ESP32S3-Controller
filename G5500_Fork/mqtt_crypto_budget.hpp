// G5500 Fork; GPL-3.0.
#pragma once
#include <atomic>
#include <cstdint>

namespace forkrot {
// Only the registered TLS task mutates the checkpoint clock. Observers read
// atomic counters; unregister only after that task leaves TLS or has joined.
class MqttCryptoBudget {
  std::atomic<uintptr_t> task_{0};
  uint32_t since_=0;
public:
  static constexpr uint32_t SliceUs=2000;
  std::atomic<uint32_t> checkpoints{0},yields{0},maxGapUs{0};
  void begin(uintptr_t task,uint32_t now) {since_=now;task_.store(task);}
  void end() {task_.store(0);}
  template<class Clock,class Pause> void released(uintptr_t task,Clock clock,Pause pause) {
    if(!task || task_.load()!=task)return;
    const uint32_t elapsed=uint32_t(clock()-since_);
    checkpoints.fetch_add(1);
    if(elapsed<SliceUs)return;
    if(elapsed>maxGapUs.load())maxGapUs.store(elapsed);
    yields.fetch_add(1);
    pause();
    since_=clock();
  }
};
inline MqttCryptoBudget mqttCryptoBudget;

// The link wrapper is enabled only by a profile that also links the real SDK
// MPI release symbol. Other architectures and host adapters remain unchanged.
#if defined(G5500_MQTT_CRYPTO_BUDGET) && G5500_MQTT_CRYPTO_BUDGET
void beginMqttCryptoBudget();
void pinMqttTlsClient(void *client);
inline void endMqttCryptoBudget() {mqttCryptoBudget.end();}
#else
inline void beginMqttCryptoBudget() {}
inline void pinMqttTlsClient(void *) {}
inline void endMqttCryptoBudget() {}
#endif
}
