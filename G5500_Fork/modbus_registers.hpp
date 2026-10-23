// Public G5500 register map v1. Multi-register commands validate before execution.
#pragma once
#include "modbus_rtu.hpp"
#include "hud.hpp"
#include <cmath>
namespace forkrot::mb {
class Registers {
  Controller &controller_;Hud &hud_;
  struct Command {uint16_t origin=0;uint32_t session=0,id=0,hash=0,at=0,request=0;Result result=Result::Invalid;bool used=false;uint32_t gesture=0;bool manual=false;} history_[8];
  uint32_t boot_=1,lastId_=0,lastRequest_=0;Result last_=Result::Ok;
  uint32_t snapshot_[32]{};
  static int32_t scaled(float v){return std::isfinite(v)?int32_t(std::lround(double(v)*1000)):0;}
  static uint8_t resultError(Result r){return r==Result::Invalid?3:r==Result::Busy?6:(r==Result::Unavailable||r==Result::NotCalibrated)?4:0;}
  uint8_t command(const uint8_t *bytes,uint32_t now) {
    const auto origin=get16(bytes);const auto session=get32(bytes+2),id=get32(bytes+6),boot=get32(bytes+10),ticket=get32(bytes+14);
    const auto op=get16(bytes+18),mask=get16(bytes+20),arg=get16(bytes+30);
    const float az=float(int32_t(get32(bytes+22)))/1000.f,el=float(int32_t(get32(bytes+26)))/1000.f;
    if(!origin||!session||!id||boot!=boot_||uint32_t(now-ticket)>2000||op<1||op>6||mask>3)return 3;
    const uint32_t hash=hudCrc(bytes,32);Command *slot=nullptr;
    for(auto &h:history_) {
      if(h.used&&h.origin==origin&&h.session==session) {
        if(h.id==id){if(h.hash!=hash)return 3;lastId_=id;last_=h.result;lastRequest_=h.request;return resultError(h.result);}
        if(int32_t(id-h.id)<=0)return 3;
        slot=&h;break;
      }
      if(!h.used||uint32_t(now-h.at)>2000)slot=&h;
    }
    if(!slot)return 6;
    auto &c=controller_;Target target{az,el,bool(mask&1),bool(mask&2)};
    if(op==1 && (!mask||!c.valid(target)))return 3;
    if((op==4&&arg>2)||(op==5&&arg>3)||(op==6&&(!mask||arg>1||std::abs(az)>1||std::abs(el)>1)))return 3;
    const bool same=slot->used&&slot->origin==origin&&slot->session==session;
    uint32_t gesture=same?slot->gesture:0;bool manual=same&&slot->manual;
    Result result=Result::Invalid;
    if(op==1)result=c.target(target,true,now);
    else if(op==2){c.stopNow(-1,now);result=c.remote?c.remote->lastResult():Result::Ok;}
    else if(op==3)result=c.execute(now);
    else if(op==4)result=c.setMode(Mode(arg),now);
    else if(op==5)result=c.preset(arg,now);
    else {
      if(!manual)gesture=id;
      result=c.manualDrive((uint64_t(0x4d42)<<32)|origin,session,gesture,id,uint8_t(mask),az,el,arg!=0,now);
      manual=arg==0;
    }
    lastId_=id;last_=result;lastRequest_=c.remote?c.remote->requestId():0;
    *slot={origin,session,id,hash,now,lastRequest_,result,true,gesture,manual};
    c.tick(now);
    return resultError(result);
  }
  void capture(uint32_t now) {
    auto &c=controller_;
    if(last_==Result::Deferred && lastRequest_) last_=hud_.result(lastRequest_).result;
    for(auto &h:history_)if(h.used&&h.result==Result::Deferred&&h.request)h.result=hud_.result(h.request).result;
    std::fill(std::begin(snapshot_),std::end(snapshot_),0);
    snapshot_[0]=(uint32_t(1)<<16)|uint16_t((c.axes[0].calibrated?1:0)|(c.axes[1].calibrated?2:0)|
        (c.axes[0].wanted?4:0)|(c.axes[1].wanted?8:0)|(c.hasDriveError()?16:0)|(c.available()?32:0));
    snapshot_[1]=(uint32_t(c.mode)<<16)|uint16_t(c.model);
    snapshot_[2]=uint32_t(scaled(c.axes[0].current));snapshot_[3]=uint32_t(scaled(c.axes[1].current));
    snapshot_[4]=uint32_t(scaled(c.axes[0].target));snapshot_[5]=uint32_t(scaled(c.axes[1].target));
    snapshot_[6]=uint32_t(scaled(c.azRange.start));snapshot_[7]=uint32_t(scaled(c.azRange.end));
    snapshot_[8]=uint32_t(scaled(c.elRange.start));snapshot_[9]=uint32_t(scaled(c.elRange.end));
    snapshot_[10]=c.stopEpoch;snapshot_[11]=lastId_;snapshot_[12]=uint32_t(last_)<<16;
    snapshot_[13]=now;snapshot_[14]=boot_;snapshot_[15]=c.rangeRevision;
  }
public:
  Registers(Controller &c,Hud &h):controller_(c),hud_(h){}
  void begin(uint32_t boot){boot_=boot?boot:1;lastId_=lastRequest_=0;last_=Result::Ok;for(auto &h:history_)h={};}
  Frame process(const Frame &request,uint32_t now) {
    if(!request.valid() || !request.data[0] || request.data[0]>247)return {};
    const uint8_t fn=request.data[1];const auto start=get16(request.data+2),count=get16(request.data+4);
    Frame response;response.data[0]=request.data[0];response.data[1]=fn;
    if(fn==3 || fn==4) {
      if(request.size!=8||!count||count>125)return exception(request,3);
      if(unsigned(start)+count>64)return exception(request,2);
      capture(now);response.data[2]=uint8_t(count*2);
      for(unsigned i=0;i<count;++i){const unsigned reg=start+i;const uint32_t v=snapshot_[reg/2];put16(response.data+3+i*2,uint16_t(reg%2?v:v>>16));}
      response.finish(3+count*2);return response;
    }
    if(fn==6) {
      if(request.size!=8)return exception(request,3);
      if(start!=0x110 || count!=1)return exception(request,start==0x110?3:2);
      controller_.stopNow(-1,now);last_=controller_.remote?controller_.remote->lastResult():Result::Ok;
      lastRequest_=controller_.remote?controller_.remote->requestId():0;return request;
    }
    if(fn==16) {
      if(request.size<9||count<1||count>123||request.data[6]!=count*2||request.size!=9+count*2)return exception(request,3);
      if(start!=0x100||count!=16)return exception(request,2);
      if(auto error=command(request.data+7,now))return exception(request,error);
      std::memcpy(response.data+2,request.data+2,4);response.finish(6);return response;
    }
    return exception(request,1);
  }
};
}
