// RTU scheduling: one outstanding transaction, token masters, unchanged slaves.
#pragma once
#include "modbus_token.hpp"
#include "modbus_mailbox.hpp"
#include "modbus_registers.hpp"
#include <memory>
namespace forkrot::mb {
class Bus {
  ModbusConfig config_;Registers registers_;
  std::unique_ptr<Mailbox[]> boxes_;
  // Allocate only addresses actually used. Empty bus scans do not retain buffers.
  std::unique_ptr<Mailbox> peers_[248];
  uint8_t misses_[248]{};
  uint32_t session_=0;
  uint8_t cursor_=0;
  Token token_;
  Frame request_,reply_,pcRequest_,pcReply_;
  uint32_t requestAt_=0,pcAt_=0,pollAt_=0;
  uint32_t replyWindowUs() const {return std::max<uint32_t>(20000u,t35(config_.uart1.baud,config_.uart1.frameBits())+5000);}
  bool master_=false,waiting_=false,gateway_=false,preferPc_=true;
  template<class Receive> void deliver(uint8_t address,const uint8_t *p,size_t n,uint32_t now,Receive receive){receive(address,p,n,now);}
public:
  uint32_t received=0,rejected=0,timeouts=0;
  Bus(Controller &c,Hud &h):registers_(c,h){}
  bool begin(const ModbusConfig &config,bool master,uint32_t boot,uint32_t now) {
    if(config.validate())return false;
    config_=config;master_=master;registers_.begin(boot);session_=boot;cursor_=0;
    for(auto &peer:peers_)peer.reset();
    std::memset(misses_,0,sizeof misses_);
    boxes_.reset(master?nullptr:new(std::nothrow) Mailbox[config.count]);if(!master&&!boxes_)return false;
    if(!master)for(unsigned i=0;i<config.count;++i)boxes_[i].begin(boot+i);
    token_.begin(config,master,boot,now);pollAt_=now;return true;
  }
  Registers &registers(){return registers_;}
  uint8_t owner() const{return token_.owner;}
  uint32_t rejectedFrames() const{return rejected+token_.rejected;}
  uint32_t passes() const{return token_.passes;}
  uint32_t recoveries() const{return token_.recoveries;}
  bool fault() const{return token_.fault;}
  bool enqueue(uint8_t address,const uint8_t *p,size_t n,uint32_t now) {
    if(!master_&&!boxes_)return false;
    if(master_) {
      if(!address)address=config_.address; // Migrate legacy UART catalogue entries.
      if(address>247||(address>=config_.first&&address<unsigned(config_.first)+config_.count))return false;
      auto &box=peers_[address];
      if(!box){box.reset(new(std::nothrow) Mailbox);if(!box)return false;box->begin(++session_);misses_[address]=0;}
      return box->enqueue(p,n,now);
    }
    return address>=config_.first&&address<unsigned(config_.first)+config_.count&&boxes_[address-config_.first].enqueue(p,n,now);
  }
  void cancel() {if(boxes_)for(unsigned i=0;i<(master_?1:config_.count);++i)boxes_[i].clear();for(auto &peer:peers_)if(peer)peer->clear();if(pcRequest_.size&&!gateway_){pcReply_=exception(pcRequest_,11);pcRequest_.size=0;}}
  bool pcRequest(const Frame &q,uint32_t now) {
    if(!master_||!q.valid()||!q.data[0]||q.data[0]>247)return false;
    if(pcRequest_.size||gateway_){if(!pcReply_.size)pcReply_=exception(q,6);return false;}
    if(q.data[0]>=config_.first&&q.data[0]<unsigned(config_.first)+config_.count){pcReply_=exception(q,2);return false;}
    const auto fn=q.data[1];
    if(fn!=3&&fn!=4&&fn!=6&&fn!=16){pcReply_=exception(q,1);return false;}
    pcRequest_=q;pcAt_=now;return true;
  }
  bool pcReply(Frame &f){if(!pcReply_.size)return false;f=pcReply_;pcReply_.size=0;return true;}
  template<class Emit,class Receive> void receive(const Frame &f,uint32_t now,uint32_t milliseconds,Emit emit,Receive receive) {
    if(!f.valid()||uint32_t(now-f.at)>80000){++rejected;return;}
    ++received;
    auto send=[&](const Frame &q){const bool ok=emit(q);if(ok)token_.activity(now);return ok;};
    if(token_.observe(f,now,send))return;
    token_.activity(now);
    if(master_) {
      if(!waiting_||f.data[0]!=request_.data[0]||(f.data[1]&0x7f)!=request_.data[1])return;
      if(gateway_) {
        if(f.data[1]==request_.data[1]) {
          if((f.data[1]==3||f.data[1]==4)&&(f.size<5||f.data[2]!=2*get16(request_.data+4)||f.size!=5+f.data[2]))return;
          if((f.data[1]==6||f.data[1]==16)&&(f.size!=8||std::memcmp(f.data+2,request_.data+2,4)))return;
        } else if(f.size!=5)return;
        pcReply_=f;gateway_=false;pcRequest_.size=0;waiting_=false;preferPc_=false;token_.pass();return;
      }
      auto &box=peers_[request_.data[0]];
      if(!box||!box->accept(f,config_.node,[&](const uint8_t *p,size_t n){deliver(f.data[0],p,n,milliseconds,receive);})){++rejected;return;}
      misses_[f.data[0]]=0;
      waiting_=false;pollAt_=now;
      if(box->complete()){cursor_=f.data[0];preferPc_=true;token_.pass();}
      return;
    }
    if(f.data[0]!=config_.address||reply_.size)return;
    if(uint32_t(now-f.at)>replyWindowUs()){++rejected;return;} // Leave time for a full RTU response before timeout.
    if(f.data[1]==Service) {
      if(f.size<25||f.data[3]!=Mailbox::Exchange){reply_=exception(f,1);reply_.at=f.at;return;}
      const auto source=f.data[4];if(source<config_.first||source>=unsigned(config_.first)+config_.count)return;
      reply_=boxes_[source-config_.first].respond(f,source,milliseconds,[&](const uint8_t *p,size_t n){deliver(source,p,n,milliseconds,receive);});
    } else reply_=registers_.process(f,milliseconds);
    reply_.at=f.at;
  }
  template<class Emit> void service(uint32_t now,uint32_t milliseconds,bool idle,Emit emit) {
    auto send=[&](const Frame &q){const bool ok=emit(q);if(ok)token_.activity(now);return ok;};
    if(reply_.size){if(uint32_t(now-reply_.at)>replyWindowUs()){reply_.size=0;++timeouts;}else if(idle&&send(reply_))reply_.size=0;return;}
    if(waiting_&&uint32_t(now-requestAt_)>=config_.transactionUs()) {
      waiting_=false;++timeouts;token_.pass();pollAt_=now;preferPc_=!gateway_;
      if(gateway_){pcReply_=exception(pcRequest_,11);pcRequest_.size=0;gateway_=false;}
      else {cursor_=request_.data[0];if(++misses_[cursor_]>=3)peers_[cursor_].reset();}
    }
    if(pcRequest_.size&&!gateway_&&uint32_t(now-pcAt_)>std::max<uint32_t>(1500000u,config_.transactionUs()*(config_.count*4+2))){pcReply_=exception(pcRequest_,11);pcRequest_.size=0;++timeouts;}
    token_.service(now,idle,waiting_,send);
    if(!master_||!idle||waiting_||!token_.canRequest(now)||uint32_t(now-pollAt_)<2000)return;
    // Alternate PC transactions and HUD exchanges, even with a continuously
    // polling PC. Otherwise the native HUD would lose freshness in a ring.
    uint8_t address=0;
    // STOP/release takes precedence across controllers, then rotate fairly.
    for(unsigned i=1;i<248;++i)if(peers_[i]&&peers_[i]->urgentPending()){address=uint8_t(i);break;}
    const bool urgent=address!=0;
    if(!address)for(unsigned i=1;i<248;++i){const auto a=uint8_t((unsigned(cursor_)+i-1)%247+1);if(peers_[a]){address=a;break;}}
    // Keep fragments on the same mailbox until its message is complete.
    if(!urgent&&request_.size&&!gateway_&&peers_[request_.data[0]]&&!peers_[request_.data[0]]->complete())address=request_.data[0];
    const bool usePc=pcRequest_.size&&!urgent&&(preferPc_||!address);
    if(!usePc&&!address){token_.pass();return;}
    const Frame next=usePc?pcRequest_:peers_[address]->request(address,config_.node,milliseconds);
    if(send(next)){request_=next;waiting_=true;gateway_=usePc;requestAt_=now;}
  }
};
}
