#pragma once
#include "hud.hpp"
namespace forkrot {
// Whole frames only. The currently transmitted COBS frame is never preempted.
// Two priority slots retain ACK/commands; replaceable telemetry cannot occupy them.
class HudUartTx {
  struct Frame { uint8_t data[HUD_FRAME_MAX+8]{}; size_t size=0; uint8_t kind=0; } active_, priority_[2], state_;
  size_t sent_=0;
  unsigned head_=0,count_=0;
public:
  bool enqueue(const uint8_t *data,size_t size) {
    if(size<5||size>HUD_FRAME_MAX)return false;
    const uint8_t kind=data[4];const bool low=kind==3||kind==8; // State or Telemetry
    Frame *slot;
    if(low){if(kind==8 && state_.size && state_.kind==3)return false;slot=&state_;}
    else {if(count_==2)return false;slot=&priority_[(head_+count_)%2];}
    const auto n=hudUartEncode(data,size,slot->data,sizeof slot->data);
    if(!n)return false;
    slot->size=n;slot->kind=kind;if(!low)++count_;return true;
  }
  template<class Write> void drain(size_t available,Write write) {
    if(sent_==active_.size){
      active_.size=0;sent_=0;
      if(count_){active_=priority_[head_];head_=(head_+1)%2;--count_;}
      else if(state_.size){active_=state_;state_.size=0;}
    }
    if(available&&sent_<active_.size)sent_+=write(active_.data+sent_,std::min(available,active_.size-sent_));
  }
};
}
