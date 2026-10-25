// MTR/1: vendor RTU function 0x41, exclusive time-bounded token ownership.
#pragma once
#include "modbus_rtu.hpp"
#include "modbus_config.hpp"
namespace forkrot::mb {
class Token {
public:
  static constexpr uint32_t LeaseUs=300000,RecoveryUs=1000000,BudgetUs=80000;
  enum Op:uint8_t {Probe=1,Claim=2,Grant=3,ProbeAck=0x81,GrantAck=0x83};
  uint32_t passes=0,recoveries=0,rejected=0;
  uint8_t owner=0;
  bool fault=false;
private:
  ModbusConfig config_;
  uint32_t term_=0,ticket_=0,lastBus_=0,lastService_=0,deadline_=0,probeAt_=0,grantAt_=0;
  uint8_t candidate_=0,probes_=0;
  bool enabled_=false,owned_=false,pending_=false,passing_=false,probing_=false;
  Frame message(uint8_t address,Op op,uint8_t to) const {
    Frame f;f.data[0]=address;f.data[1]=Service;f.data[2]=17;f.data[3]=op;
    f.data[4]=config_.node;f.data[5]=to;f.data[6]=config_.first;f.data[7]=config_.count;
    put32(f.data+8,term_);put32(f.data+12,ticket_);put16(f.data+16,config_.tokenLeaseUs()/1000);put16(f.data+18,0x4d54);f.finish(20);return f;
  }
  uint8_t next(uint8_t address) const {return unsigned(address)+1>=unsigned(config_.first)+config_.count?config_.first:uint8_t(address+1);}
  void lose(uint32_t now){owned_=pending_=passing_=probing_=false;owner=0;lastBus_=now;}
public:
  void begin(const ModbusConfig &c,bool master,uint32_t seed,uint32_t now) {
    config_=c;enabled_=master;term_=seed?seed:1;ticket_=0;
    // Start without authority. A rebooted participant must observe a quiet bus.
    lastService_=lastBus_=now;owned_=pending_=passing_=probing_=false;owner=0;fault=false;
  }
  void activity(uint32_t now){lastBus_=now;}
  bool owns(uint32_t now) const {return enabled_&&!fault&&owned_&&int32_t(deadline_-now)>0;}
  bool canRequest(uint32_t now) const {return owns(now)&&!passing_&&uint32_t(deadline_-now)>=config_.transactionUs();}
  void pass(){passing_=true;}
  template<class Emit> bool observe(const Frame &f,uint32_t now,Emit emit) {
    if(!f.valid() || f.size!=22 || f.data[1]!=Service || f.data[2]!=17 || get16(f.data+18)!=0x4d54)return false;
    const uint32_t quiet=now-lastBus_;lastBus_=now;
    if(f.data[6]!=config_.first || f.data[7]!=config_.count || get16(f.data+16)!=config_.tokenLeaseUs()/1000) {fault=true;lose(now);return true;}
    const uint8_t from=f.data[4],to=f.data[5],op=f.data[3];
    if(from<config_.first || from>=unsigned(config_.first)+config_.count || to<config_.first ||
        to>=unsigned(config_.first)+config_.count || f.data[0]!=to){++rejected;return true;}
    const uint32_t term=get32(f.data+8),ticket=get32(f.data+12);
    if(op==Claim) {
      // Claims must follow a bus-wide recovery silence. A valid conflicting
      // claim stops a current owner rather than creating two transmitters.
      // After recovery silence all old leases/queued frames have expired.
      // Accept a rebooted coordinator's new epoch even if its number is lower.
      if(owned_ || pending_) {lose(now);++rejected;return true;}
      if(from!=to || !term || !ticket || quiet<config_.tokenRecoveryUs()-100000){++rejected;return true;}
      term_=term;ticket_=ticket;owner=from;return true;
    }
    if(op==Grant) {
      if(from==to || !term || !ticket || (owner && (owner!=from || term!=term_)) || (term==term_ && int32_t(ticket-ticket_)<=0)) {++rejected;return true;}
      if(owned_) {lose(now);++rejected;return true;}
      term_=term;ticket_=ticket;owner=to;probing_=false;
      if(enabled_ && to==config_.node) {
        Frame ack=f;ack.data[3]=GrantAck;ack.finish(20);
        if(emit(ack)){pending_=true;grantAt_=now;}
      }
      return true;
    }
    if(op==Probe && enabled_ && to==config_.node) {
      if(owner && owner!=from){++rejected;return true;}
      Frame ack=f;ack.data[3]=ProbeAck;ack.finish(20);emit(ack);return true;
    }
    if(op==ProbeAck && owns(now) && probing_ && from==config_.node && to==candidate_ && term==term_ && ticket==ticket_) {
      // Relinquish BEFORE putting the grant on the wire. A lost ACK grants no
      // right to retry or resume. Recovery waits out every possible old lease.
      if(!++ticket_)++ticket_;
      Frame grant=message(candidate_,Grant,candidate_);
      owned_=false;pending_=passing_=probing_=false;probes_=0;
      owner=emit(grant)?candidate_:0;if(owner)++passes;
      return true;
    }
    return true; // Grant ACK is informational; the old owner remains silent.
  }
  template<class Emit> void service(uint32_t now,bool idle,bool outstanding,Emit emit) {
    const uint32_t gap=now-lastService_;lastService_=now;
    if(!enabled_ || fault)return;
    if(gap>40000) {lose(now);return;} // A stalled loop cannot use an old lease.
    if(pending_ && idle && uint32_t(now-grantAt_)>=6000) {
      pending_=false;owned_=true;owner=config_.node;deadline_=now+config_.tokenLeaseUs();passing_=probing_=false;probes_=0;
    }
    if(owned_) {
      if(int32_t(now-deadline_)>=0){lose(now);return;}
      if(config_.count==1 && !outstanding){deadline_=now+config_.tokenLeaseUs();passing_=false;return;}
      if(uint32_t(deadline_-now)<config_.transactionUs())passing_=true;
      if(!passing_ || outstanding || !idle)return;
      if(probing_) {
        if(uint32_t(now-probeAt_)<std::max<uint32_t>(30000u,config_.uart1.wireUs(44)+10000u))return;
        probing_=false;candidate_=next(candidate_);++probes_;
      } else if(!probes_)candidate_=next(config_.node);
      if(candidate_==config_.node || probes_>=config_.count-1) {
        passing_=false;probes_=0;deadline_=now+config_.tokenLeaseUs();return;
      }
      if(emit(message(candidate_,Probe,candidate_))){probing_=true;probeAt_=now;}
      return;
    }
    if(pending_ || !idle || outstanding)return;
    const uint32_t quiet=now-lastBus_,slot=uint32_t(config_.node-config_.first)*50000;
    if(quiet<config_.tokenRecoveryUs()+slot)return;
    if(quiet>=config_.tokenRecoveryUs()+slot+20000){lastBus_=now;return;} // Never send a missed slot late.
    ++term_;if(!term_)++term_;if(!++ticket_)++ticket_;
    Frame claim=message(config_.node,Claim,config_.node);
    if(emit(claim)) {owned_=true;owner=config_.node;deadline_=now+config_.tokenLeaseUs();probes_=0;passing_=false;++recoveries;}
  }
};
}
