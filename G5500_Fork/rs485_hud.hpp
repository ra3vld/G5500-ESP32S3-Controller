// Two-node half-duplex HUD transport. HUD alone initiates transactions. GPL-3.0.
#pragma once
#include "hud.hpp"
#include <algorithm>
namespace forkrot {
class Rs485Hud {
  static constexpr size_t RawMax=HUD_FRAME_MAX+21, WireMax=RawMax+RawMax/254+3;
  struct Frame { uint8_t data[HUD_FRAME_MAX]{}; size_t size=0; uint32_t at=0; } stop_, manual_, queue_[2], state_;
  uint8_t encoded_[WireMax]{}, raw_[RawMax]{}, tx_[WireMax]{};
  size_t encodedSize_=0, txSize_=0, txSent_=0;
  unsigned head_=0, count_=0, retries_=0;
  uint64_t session_=0;
  uint32_t seq_=0, lastRequestCrc_=0, lastRx_=0, at_=0;
  bool master_=false, overflow_=false, waiting_=false, transmitting_=false, haveRequest_=false;
  static uint32_t get32(const uint8_t *p) { uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(p[i])<<(8*i);return v; }
  static void put32(uint8_t *p,uint32_t v) { for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(8*i)); }
  static uint64_t get64(const uint8_t *p) { return uint64_t(get32(p))|(uint64_t(get32(p+4))<<32); }
  static bool urgent(const uint8_t *p,size_t n) {
    if(n<49 || p[3]<'1' || p[3]>'4') return false;
    const size_t offset=p[3]=='1'?45:49;
    if(n<=offset+5) return false;
    return ((p[4]==4 || p[4]==9) && (p[offset]==uint8_t(ControlOp::Stop) || p[offset]==uint8_t(ControlOp::ReleaseJog))) ||
        (p[4]==15 && p[offset+5]==1);
  }
  void clearQueue() { stop_.size=manual_.size=state_.size=0;count_=head_=0; }
  size_t take(uint8_t *out,uint32_t now) {
    Frame *f=nullptr;
    if(stop_.size) f=&stop_;
    else if(manual_.size) f=&manual_;
    else while(count_) {
      auto &item=queue_[head_];head_=(head_+1)%2;--count_;
      if(uint32_t(now-item.at)<150) { f=&item;break; }
    }
    if(!f && state_.size) f=&state_;
    if(!f) return 0;
    const size_t n=uint32_t(now-f->at)<(f==&stop_?500u:150u)?f->size:0;
    if(n) std::memcpy(out,f->data,n);
    f->size=0;return n;
  }
  void prepare(uint8_t kind,size_t payload) {
    std::memcpy(raw_,"R485",4);raw_[4]=kind;
    put32(raw_+5,uint32_t(session_));put32(raw_+9,uint32_t(session_>>32));put32(raw_+13,seq_);
    const size_t n=17+payload;put32(raw_+n,hudCrc(raw_,n));
    size_t write=1,codeAt=0;uint8_t code=1;
    for(size_t i=0;i<n+4;++i) {
      if(!raw_[i]) { tx_[codeAt]=code;code=1;codeAt=write++; }
      else { tx_[write++]=raw_[i];if(++code==255) {tx_[codeAt]=code;code=1;codeAt=write++;} }
    }
    tx_[codeAt]=code;tx_[write++]=0;txSize_=write;txSent_=0;transmitting_=true;
  }
public:
  uint32_t received=0, rejected=0, timeouts=0, dropped=0;
  void begin(bool master,uint64_t boot,uint32_t now) {
    master_=master;session_=master?boot:0;seq_=0;haveRequest_=false;
    waiting_=transmitting_=overflow_=false;encodedSize_=txSize_=txSent_=0;
    lastRx_=now;at_=now-20;clearQueue();
  }
  bool enqueue(const uint8_t *p,size_t n,uint32_t now) {
    if(n<49 || n>HUD_FRAME_MAX) return false;
    Frame *f;
    if(urgent(p,n)) { clearQueue();f=&stop_; }
    else if(p[4]==15) f=&manual_; // Replace an older continuous manual sample.
    else if(p[4]==8 || p[4]==14) f=&state_;
    else { if(count_==2) {++dropped;return false;}f=&queue_[(head_+count_++)%2]; }
    std::memcpy(f->data,p,n);f->size=n;f->at=now;return true;
  }
  template<class Receive> void feed(uint8_t byte,uint32_t now,Receive receive) {
    if(byte) { if(encodedSize_<sizeof encoded_)encoded_[encodedSize_++]=byte;else overflow_=true;return; }
    const size_t n=encodedSize_;encodedSize_=0;
    if(overflow_) {overflow_=false;++rejected;return;}
    if(!n) return;
    size_t in=0,used=0;
    while(in<n) {
      const uint8_t code=encoded_[in++];
      if(!code || in+code-1>n) {++rejected;return;}
      for(unsigned i=1;i<code;++i) {if(used==sizeof raw_) {++rejected;return;}raw_[used++]=encoded_[in++];}
      if(code<255 && in<n) {if(used==sizeof raw_) {++rejected;return;}raw_[used++]=0;}
    }
    if(used<21 || std::memcmp(raw_,"R485",4) || get32(raw_+used-4)!=hudCrc(raw_,used-4)) {++rejected;return;}
    const auto session=get64(raw_+5);const auto seq=get32(raw_+13);const auto crc=get32(raw_+used-4);
    if(!session || !seq || raw_[4]!=(master_?2:1)) {++rejected;return;}
    if(master_) {
      if(!waiting_ || session!=session_ || seq!=seq_) {++rejected;return;}
      waiting_=false;lastRx_=now;at_=now;retries_=0;++received;
      if(used>21) receive(raw_+17,used-21);
    } else {
      // Do not overwrite an in-flight response. Identical requests get the
      // cached response without executing a motor command twice.
      if(transmitting_) {++rejected;return;}
      if(haveRequest_ && session==session_ && seq==seq_) {
        if(crc!=lastRequestCrc_) {++rejected;return;}
        txSent_=0;transmitting_=true;at_=now;return;
      }
      if(haveRequest_ && session==session_ && int32_t(seq-seq_)<=0) {++rejected;return;}
      if(session!=session_ || uint32_t(now-lastRx_)>=500) clearQueue();
      session_=session;seq_=seq;lastRequestCrc_=crc;haveRequest_=true;lastRx_=now;at_=now;++received;
      if(used>21) receive(raw_+17,used-21);
      // receive() can enqueue the ACK synchronously, before this response.
      const size_t payload=take(raw_+17,now);prepare(2,payload);
    }
  }
  template<class Write,class Idle> void service(uint32_t now,size_t available,Write write,Idle idle) {
    if(transmitting_) {
      // The requester must finish the delimiter's stop bit and release DE.
      if(!master_ && !txSent_ && uint32_t(now-at_)<2) return;
      if(txSent_<txSize_ && available) txSent_+=write(tx_+txSent_,std::min(available,txSize_-txSent_));
      if(txSent_==txSize_ && idle()) {transmitting_=false;at_=now;}
      return;
    }
    if(!master_) return;
    if(waiting_) {
      if(uint32_t(now-at_)<180) return;
      ++timeouts;
      if(stop_.size) { waiting_=false;at_=now-20;return; }
      if(retries_++<2) { txSent_=0;transmitting_=true;return; }
      waiting_=false;clearQueue();at_=now;return;
    }
    if(uint32_t(now-at_)<20) return;
    if(uint32_t(now-lastRx_)>=500) clearQueue();
    if(++seq_==0) ++seq_;
    const size_t payload=take(raw_+17,now);prepare(1,payload);waiting_=true;retries_=0;
  }
};
}
