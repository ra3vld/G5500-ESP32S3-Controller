// HUD datagrams carried in bounded Modbus vendor-function transactions. GPL-3.0.
#pragma once
#include "modbus_rtu.hpp"
#include "hud.hpp"
namespace forkrot::mb {
class Mailbox {
  static constexpr size_t Chunk=192;
  struct Message {uint8_t data[HUD_FRAME_MAX]{};uint16_t size=0;uint32_t at=0;} tx_,stop_,manual_,queue_[2],state_;
  unsigned head_=0,count_=0;
  uint8_t rx_[HUD_FRAME_MAX]{};
  uint32_t session_=0,peerSession_=0,txId_=0,rxId_=0;
  uint16_t sent_=0,rxSize_=0,rxOffset_=0;
  static bool urgent(const uint8_t *p,size_t n) {
    if(n<55)return false;
    const size_t o=p[3]=='1'?45:49;
    return n>o+5 && (((p[4]==4||p[4]==9)&&(p[o]==uint8_t(ControlOp::Stop)||p[o]==uint8_t(ControlOp::ReleaseJog))) || (p[4]==15&&p[o+5]==1));
  }
  void take(uint32_t now) {
    if(tx_.size)return;
    Message *m=nullptr;
    if(stop_.size)m=&stop_;
    else if(manual_.size)m=&manual_;
    else while(count_) {auto &q=queue_[head_];head_=(head_+1)%2;--count_;if(uint32_t(now-q.at)<900){m=&q;break;}}
    if(!m&&state_.size)m=&state_;
    if(!m)return;
    if(uint32_t(now-m->at)<(m==&stop_?900u:m==&manual_?100u:900u)) {tx_=*m;if(!++txId_)++txId_;sent_=0;}
    m->size=0;
  }
  template<class Receive> bool append(uint32_t id,uint16_t total,uint16_t offset,const uint8_t *data,size_t size,Receive receive) {
    if(!id)return !total&&!offset&&!size;
    if(!total||total>HUD_FRAME_MAX||offset>total||size>size_t(total-offset))return false;
    if(id!=rxId_) {if(offset)return false;rxId_=id;rxSize_=total;rxOffset_=0;}
    if(total!=rxSize_ || offset>rxOffset_)return false;
    if(offset<rxOffset_)return offset+size<=rxOffset_ && !std::memcmp(rx_+offset,data,size);
    if(rxOffset_==rxSize_)return size==0; // Completed frames are never delivered twice.
    std::memcpy(rx_+offset,data,size);rxOffset_=uint16_t(offset+size);
    if(rxOffset_==rxSize_)receive(rx_,rxSize_);
    return true;
  }
  void header(Frame &f,uint8_t address,uint8_t source,uint8_t op,uint32_t session) {
    f.data[0]=address;f.data[1]=Service;f.data[3]=op;f.data[4]=source;put32(f.data+5,session);
  }
  static void finish(Frame &f,size_t n){f.data[2]=uint8_t(20+n);f.finish(23+n);}
public:
  static constexpr uint8_t Exchange=0x10,Reply=0x90;
  void begin(uint32_t session){session_=session?session:1;txId_=session_;}
  void clear(){tx_.size=stop_.size=manual_.size=state_.size=0;count_=head_=0;sent_=0;}
  bool enqueue(const uint8_t *p,size_t n,uint32_t now) {
    if(n<49||n>HUD_FRAME_MAX)return false;
    Message *m;
    if(urgent(p,n)){clear();m=&stop_;}
    else if(p[4]==15)m=&manual_;
    else if(p[4]==8||p[4]==14)m=&state_;
    else {if(count_==2)return false;m=&queue_[(head_+count_++)%2];}
    std::memcpy(m->data,p,n);m->size=uint16_t(n);m->at=now;return true;
  }
  Frame request(uint8_t address,uint8_t source,uint32_t now) {
    take(now);Frame f;header(f,address,source,Exchange,session_);
    put32(f.data+9,tx_.size?txId_:0);put16(f.data+13,tx_.size);put16(f.data+15,sent_);
    put32(f.data+17,rxId_);put16(f.data+21,rxOffset_);
    const size_t n=std::min<size_t>(Chunk,tx_.size-sent_);std::memcpy(f.data+23,tx_.data+sent_,n);finish(f,n);return f;
  }
  template<class Receive> Frame respond(const Frame &q,uint8_t source,uint32_t now,Receive receive) {
    if(!q.valid()||q.size<25||q.data[1]!=Service||q.data[3]!=Exchange||q.data[2]!=q.size-5||q.data[4]!=source)return {};
    const auto session=get32(q.data+5);if(!session)return {};
    if(session!=peerSession_){clear();peerSession_=session;rxId_=rxSize_=rxOffset_=0;}
    const auto id=get32(q.data+9);const auto total=get16(q.data+13),offset=get16(q.data+15);
    if(!append(id,total,offset,q.data+23,q.size-25,receive))return exception(q,3);
    const auto ackId=get32(q.data+17);const auto ack=get16(q.data+21);
    if(tx_.size&&ackId==txId_&&ack==tx_.size)tx_.size=0;
    take(now);uint16_t next=tx_.size&&ackId==txId_?ack:0;
    if(next>tx_.size)return exception(q,3);
    Frame f;header(f,q.data[0],source,Reply,session);
    put32(f.data+9,rxId_);put16(f.data+13,rxOffset_);put32(f.data+15,tx_.size?txId_:0);
    put16(f.data+19,tx_.size);put16(f.data+21,next);
    const size_t n=std::min<size_t>(Chunk,tx_.size-next);std::memcpy(f.data+23,tx_.data+next,n);finish(f,n);return f;
  }
  template<class Receive> bool accept(const Frame &f,uint8_t source,Receive receive) {
    if(!f.valid()||f.size<25||f.data[1]!=Service||f.data[3]!=Reply||f.data[2]!=f.size-5||f.data[4]!=source||get32(f.data+5)!=session_)return false;
    const auto ackId=get32(f.data+9);const auto ack=get16(f.data+13);
    if(tx_.size) {if(ackId!=txId_||ack<sent_||ack>tx_.size||ack>sent_+Chunk)return false;sent_=ack;if(sent_==tx_.size){tx_.size=0;sent_=0;}}
    return append(get32(f.data+15),get16(f.data+19),get16(f.data+21),f.data+23,f.size-25,receive);
  }
  bool complete() const {return !tx_.size && (!rxId_||rxOffset_==rxSize_);}
};
}
