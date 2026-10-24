#pragma once
#include "hud.hpp"
namespace forkrot {
// Whole frames only. The currently transmitted COBS frame is never preempted.
// Two priority slots retain ACK/commands; replaceable telemetry cannot occupy them.
class HudUartTx {
  struct Frame { uint8_t data[HUD_FRAME_MAX+8]{}; size_t size=0; uint8_t kind=0; } active_, stop_, manual_, query_, priority_[2], state_;
  size_t sent_=0;
  unsigned head_=0,count_=0;
  bool paced_=false;
public:
  void setPaced(bool paced) { paced_=paced; }
  bool enqueue(const uint8_t *data,size_t size) {
    if(size<5||size>HUD_FRAME_MAX)return false;
    const uint8_t kind=data[4];const bool low=kind==3||kind==8||kind==12||kind==14;
    const size_t offset=data[3]=='1'?45:49;
    const bool stop=size>offset+5 && (((kind==4||kind==9) &&
        (data[offset]==uint8_t(ControlOp::Stop)||data[offset]==uint8_t(ControlOp::ReleaseJog))) ||
        (kind==15 && (data[offset+5]&1)));
    Frame *slot;
    bool priority=false;
    if(stop){count_=head_=0;manual_.size=query_.size=state_.size=0;slot=&stop_;}
    else if(kind==15||kind==16)slot=&manual_;
    else if(paced_ && (kind==1||kind==2||kind==7||kind==11||kind==13))slot=&query_;
    else if(low){if((kind==8||kind==14) && state_.size && (state_.kind==3||state_.kind==12))return false;slot=&state_;}
    else {if(count_==2)return false;slot=&priority_[(head_+count_)%2];priority=true;}
    const auto n=hudUartEncode(data,size,slot->data,sizeof slot->data);
    if(!n)return false;
    slot->size=n;slot->kind=kind;if(priority)++count_;return true;
  }
  bool pending() const { return sent_<active_.size || stop_.size || manual_.size || count_ || query_.size || state_.size; }
  bool frameDone() const { return active_.size && sent_==active_.size; }
  template<class Write> void drain(size_t available,Write write) {
    if(sent_==active_.size){
      active_.size=0;sent_=0;
      if(stop_.size){active_=stop_;stop_.size=0;}
      else if(manual_.size){active_=manual_;manual_.size=0;}
      else if(count_){active_=priority_[head_];head_=(head_+1)%2;--count_;}
      else if(query_.size){active_=query_;query_.size=0;}
      else if(state_.size){active_=state_;state_.size=0;}
    }
    if(available&&sent_<active_.size)sent_+=write(active_.data+sent_,std::min(available,active_.size-sent_));
  }
};
}
