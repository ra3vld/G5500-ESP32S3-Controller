// Bounded binary HUD mailboxes. No allocations, SDK calls or waits. GPL-3.0.
#pragma once
#include "hud.hpp"
#include <atomic>
#include <algorithm>
namespace forkrot {
inline uint64_t mqttHudId(const uint8_t *p) {
  uint64_t id=0;for(unsigned i=0;i<8;++i)id|=uint64_t(p[i])<<(i*8);return id;
}
inline bool mqttHudFrame(const uint8_t *p,size_t n) {
  return n>=49 && n<=HUD_FRAME_MAX && !std::memcmp(p,"HUD",3) && p[3]>='1' && p[3]<='5' &&
      p[4]>=1 && p[4]<=16 && mqttHudId(p+5) && !(mqttHudId(p+5)>>48) && !(mqttHudId(p+13)>>48);
}
inline bool mqttHudStop(const uint8_t *p,size_t n) {
  const size_t at=p[3]=='1'?45:49;
  return n>at+5 && (((p[4]==4 || p[4]==9) &&
      (p[at]==uint8_t(ControlOp::Stop) || p[at]==uint8_t(ControlOp::ReleaseJog))) ||
      (p[4]==15 && (p[at+5]&1)));
}
inline unsigned mqttHudPriority(uint8_t kind) {
  return kind==5 || kind==10 || kind==16 ? 3 : kind==4 || kind==9 || kind==15 || kind==6 ? 2 : 0;
}
#if HUD_SUPPORT
class MqttHudQueue {
  enum class State:uint8_t { Empty,Writing,Queued,Sending };
  template<size_t N> struct Slot {
    std::atomic<State> state{State::Empty};
    uint8_t bytes[N]{};
    uint32_t epoch=0,at=0;
    uint16_t size=0;
  };
  Slot<HUD_FRAME_MAX> normal_;
  Slot<80> stops_[2];
  template<size_t N> bool offer(Slot<N> &s,const uint8_t *p,size_t n,uint32_t epoch,uint32_t now) {
    if(n>N)return false;
    auto expected=s.state.load(std::memory_order_acquire);
    if(expected!=State::Empty && expected!=State::Queued)return false;
    const auto previous=expected;
    if(!s.state.compare_exchange_strong(expected,State::Writing,std::memory_order_acq_rel))return false;
    if(previous==State::Queued && s.epoch==epoch) {
      const auto oldPriority=mqttHudPriority(s.bytes[4]),priority=mqttHudPriority(p[4]);
      const bool same=!std::memcmp(p+5,s.bytes+5,32);
      // Coalesce only state/query/manual samples from the same conversation.
      // Distinct commands and independent axis STOPs keep their own results.
      if(priority<oldPriority || (priority==oldPriority &&
          (!same || (priority && p[4]!=15) || (N==80 && std::memcmp(p,s.bytes,n))))) {
        s.state.store(State::Queued,std::memory_order_release);return false;
      }
    }
    std::memcpy(s.bytes,p,n);s.size=uint16_t(n);s.epoch=epoch;s.at=now;
    s.state.store(State::Queued,std::memory_order_release);return true;
  }
  template<size_t N,class Send> bool drain(Slot<N> &s,uint32_t epoch,uint32_t now,Send send) {
    auto expected=State::Queued;
    if(!s.state.compare_exchange_strong(expected,State::Sending,std::memory_order_acq_rel))return false;
    const uint32_t age=uint32_t(now-s.at);
    const bool obsolete=s.epoch!=epoch || age>=(s.bytes[4]==15 && !mqttHudStop(s.bytes,s.size)?100u:500u);
    const bool complete=obsolete || send(s.bytes,s.size);
    s.state.store(complete?State::Empty:State::Queued,std::memory_order_release);return !obsolete;
  }
public:
  bool pending() const {
    if(normal_.state.load(std::memory_order_acquire)!=State::Empty)return true;
    for(const auto &s:stops_)if(s.state.load(std::memory_order_acquire)!=State::Empty)return true;
    return false;
  }
  bool offer(const uint8_t *p,size_t n,uint32_t epoch,uint32_t now) {
    if(!mqttHudFrame(p,n))return false;
    if(!mqttHudStop(p,n))return offer(normal_,p,n,epoch,now);
    for(auto &s:stops_)if(offer(s,p,n,epoch,now)) {
      // STOP also cancels unsent motion; the HUD protocol rejects an older
      // command already in flight using its source/session/serial barrier.
      auto expected=State::Queued;
      if(normal_.state.compare_exchange_strong(expected,State::Writing,std::memory_order_acq_rel)) {
        uint32_t oldSerial=0,serial=0;
        std::memcpy(&oldSerial,normal_.bytes+37,4);std::memcpy(&serial,p+37,4);
        const bool same=!std::memcmp(normal_.bytes+5,p+5,32) && int32_t(oldSerial-serial)<=0;
        normal_.state.store(same?State::Empty:State::Queued,std::memory_order_release);
      }
      return true;
    }
    return false;
  }
  template<class Send> void drain(uint32_t epoch,uint32_t now,Send send) {
    for(auto &s:stops_)drain(s,epoch,now,send);
    drain(normal_,epoch,now,send);
  }
};
#else
class MqttHudQueue {
public:
  bool pending() const { return false; }
  bool offer(const uint8_t *,size_t,uint32_t,uint32_t) { return false; }
  template<class Send> void drain(uint32_t,uint32_t,Send) {}
};
#endif
static_assert(sizeof(MqttHudQueue)<=1024,"Keep HUD MQTT queues below 1 KiB per direction");
}
