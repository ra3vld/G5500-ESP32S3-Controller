// One bounded producer/consumer slot; no SDK locks on the controller. GPL-3.0.
#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string_view>
namespace forkrot {
class MqttPublishMailbox {
public:
  struct Message {
    char topic[192]{},payload[1024]{};
    uint32_t epoch=0,at=0;
    uint16_t size=0;
    int qos=0;
    bool retain=false;
  };
  // A retained value can replace an older unsent value for the same topic.
  // Different topics wait for the single slot instead of building a backlog.
  bool offer(const char *topic,std::string_view payload,bool retain,int qos,uint32_t epoch,uint32_t now) {
    if(!topic || std::strlen(topic)>=sizeof message_.topic || payload.size()>sizeof message_.payload)return false;
    auto expected=state_.load(std::memory_order_acquire);
    if(expected!=State::Empty && expected!=State::Queued)return false;
    const auto previous=expected;
    if(!state_.compare_exchange_strong(expected,State::Writing,std::memory_order_acq_rel))return false;
    if(previous==State::Queued && (std::strcmp(topic,message_.topic) || !retain ||
        !message_.retain || qos!=message_.qos)) {
      state_.store(State::Queued,std::memory_order_release);return false;
    }
    std::strcpy(message_.topic,topic);
    if(!payload.empty())std::memcpy(message_.payload,payload.data(),payload.size());
    message_.size=uint16_t(payload.size());message_.retain=retain;message_.qos=qos;
    message_.epoch=epoch;message_.at=now;
    state_.store(State::Queued,std::memory_order_release);return true;
  }
  // Called only on the MQTT task. A failed enqueue retains the same message;
  // session changes invalidate it before any publication on a new connection.
  template<class Send> void drain(uint32_t epoch,uint32_t now,Send send) {
    auto expected=State::Queued;
    if(!state_.compare_exchange_strong(expected,State::Sending,std::memory_order_acq_rel))return;
    const bool obsolete=message_.epoch!=epoch || (!message_.qos && uint32_t(now-message_.at)>=5000);
    const bool complete=obsolete || send(message_);
    state_.store(complete?State::Empty:State::Queued,std::memory_order_release);
  }
  bool empty() const { return state_.load(std::memory_order_acquire)==State::Empty; }
private:
  enum class State : uint8_t { Empty,Writing,Queued,Sending };
  Message message_;
  std::atomic<State> state_{State::Empty};
};
}
