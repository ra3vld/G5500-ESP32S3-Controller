// G5500 Fork, GPL-3.0; shared by the ESP32 firmware and host simulator.
#include "control.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cerrno>
#include <cstdio>
namespace forkrot {
Controller::Controller(){
 for(unsigned i=0;i<4;++i) presets[i]={std::to_string(i*90)+"°",{float(i*90),0,true,true}};
}
const char* Controller::modeName(Mode m){return m==Mode::Auto?"auto":m==Mode::AutoHour?"auto_1h":"manual";}
const char* Controller::resultName(Result r){
 switch(r){case Result::Ok:return "ok";case Result::Pending:return "pending";case Result::Ignored:return "ignored";case Result::NotCalibrated:return "not_calibrated";default:return "invalid";}
}
void Controller::feedback(float az,float el,bool ac,bool ec){
 axes[0].current=az;axes[0].calibrated=ac&&std::isfinite(az);
 axes[1].current=model==Model::G800DXA?0:el;
 axes[1].calibrated=model==Model::G5500&&ec&&std::isfinite(el);
}
bool Controller::valid(Target t) const {
 return (t.hasAz||t.hasEl)&&(!t.hasAz||(std::isfinite(t.az)&&t.az>=0&&t.az<=maximum))&&
 (!t.hasEl||(std::isfinite(t.el)&&t.el>=0&&t.el<=180&&(model==Model::G5500||t.el==0)));
}
Result Controller::start(Target t,uint32_t){
 if(!valid(t))return Result::Invalid;
 if((t.hasAz&&!axes[0].calibrated)||(t.hasEl&&model==Model::G5500&&!axes[1].calibrated))return Result::NotCalibrated;
 fault.clear();targetKnown=true;
 for(unsigned i=0;i<2;++i){
  if((i==0?t.hasAz:t.hasEl)&&(i==0||model==Model::G5500)){
   Axis& a=axes[i];a.target=i?t.el:t.az;
   float d=a.target-a.current;a.wanted=std::abs(d)<=0.5f?0:d>0?1:-1;a.tracking=a.wanted!=0;
  }
 }
 return Result::Ok;
}
Result Controller::target(Target t,bool external,uint32_t now){
 expire(now);
 if(!valid(t))return Result::Invalid;
 if(external&&presetActive)return Result::Ignored;
 cancelSequence();
 if(external&&mode==Mode::Manual){pending=t;pendingValid=true;return Result::Pending;}
 if(!external){presetActive=false;presetSlot=-1;}
 auto r=start(t,now);if(r==Result::Ok)pendingValid=false;return r;
}
Result Controller::jog(unsigned axis,int dir,bool external,uint32_t now){
 expire(now);
 if(axis>1||(axis==1&&model==Model::G800DXA)||dir<-1||dir>1)return Result::Invalid;
 if(dir==0){stop(axis);return Result::Ok;}
 if(external&&presetActive)return Result::Ignored;
 cancelSequence();if(!external){presetActive=false;presetSlot=-1;}
 axes[axis].tracking=false;axes[axis].wanted=dir;fault.clear();return Result::Ok;
}
void Controller::stop(int axis){
 if(axis<-1||axis>1)return;
 ++stopEpoch;
 cancelSequence();pendingValid=false;presetActive=false;presetSlot=-1;
 for(int i=0;i<2;++i)if(axis<0||axis==i){axes[i].wanted=0;axes[i].tracking=false;}
}
Result Controller::execute(uint32_t now){
 expire(now);
 if(presetActive)return Result::Ignored;
 if(!pendingValid)return Result::Invalid;
 auto r=start(pending,now);
 if(r==Result::Ok){
  pendingValid=false;
  if(!pendingSequence_.empty()){sequence_=pendingSequence_;pendingSequence_.clear();sequenceIndex_=0;sequenceAt_=now;}
 }
 return r;
}
Result Controller::setMode(Mode m,uint32_t now){
 mode=m;hourAt_=now;
 if(m==Mode::Manual){sequence_.clear();return Result::Ok;}
 if(pendingValid){auto r=execute(now);if(r==Result::NotCalibrated){fault="target_not_calibrated";return Result::Ok;}return r;}
 return Result::Ok;
}
void Controller::restoreMode(Mode m){mode=m==Mode::Auto?Mode::Auto:Mode::Manual;}
void Controller::setModel(Model m){stop();model=m;mode=Mode::Manual;targetKnown=false;fault.clear();for(auto& a:axes){a.calibrated=false;a.hasRun=false;}}
void Controller::setMaximum(float v){if(v!=360&&v!=450)return;stop();maximum=v;}
Result Controller::savePreset(unsigned slot,const std::string& label,Target t){
 if(slot>=4||label.empty()||label.size()>48||!valid(t))return Result::Invalid;
 for(unsigned char ch:label)if(ch<32)return Result::Invalid;
 presets[slot]={label,t};return Result::Ok;
}
Result Controller::preset(unsigned slot,uint32_t now){
 expire(now);
 if(slot>=4)return Result::Invalid;
 if(!valid(presets[slot].target))return Result::Invalid;
 auto r=start(presets[slot].target,now);
 if(r==Result::Ok){cancelSequence();pendingValid=false;presetSlot=slot;presetActive=true;}
 return r;
}
Result Controller::sequence(const std::vector<Target>& points,uint32_t interval,uint32_t now){
 expire(now);
 if(points.empty()||points.size()>16||interval<1000||interval>999000)return Result::Invalid;
 for(auto t:points)if(!valid(t))return Result::Invalid;
 if(presetActive)return Result::Ignored;
 cancelSequence();interval_=interval;
 if(mode==Mode::Manual){pendingSequence_=points;pending=points[0];pendingValid=true;return Result::Pending;}
 auto r=start(points[0],now);if(r==Result::Ok){sequence_=points;sequenceIndex_=0;sequenceAt_=now;pendingValid=false;}return r;
}
void Controller::cancelSequence(){sequence_.clear();pendingSequence_.clear();}
uint32_t Controller::remaining(uint32_t now)const{auto elapsed=uint32_t(now-hourAt_);return mode==Mode::AutoHour&&elapsed<3600000?3600000-elapsed:0;}
bool Controller::moving()const{for(auto a:axes)if(a.wanted||a.direction)return true;return false;}
void Controller::output(unsigned i,int dir,uint32_t now){
 auto& a=axes[i];
 if(a.direction!=0&&a.direction!=dir){a.previous=a.direction;a.direction=0;a.offAt=now;}
 if(dir!=0&&a.direction==0){
  if(a.previous&&a.previous!=dir&&uint32_t(now-a.offAt)<300)return;
  a.direction=dir;if(!a.hasRun){a.runAt=now;a.hasRun=true;}
 }
 if(dir==0&&uint32_t(now-a.offAt)>=300)a.hasRun=false;
}
void Controller::expire(uint32_t now){
 if(mode==Mode::AutoHour&&!remaining(now)){mode=Mode::Manual;sequence_.clear();}
}
void Controller::tick(uint32_t now){
 expire(now);
 if(!sequence_.empty()&&uint32_t(now-sequenceAt_)>=interval_){
  if(++sequenceIndex_>=sequence_.size())sequence_.clear();
  else {auto r=start(sequence_[sequenceIndex_],now);if(r!=Result::Ok){sequence_.clear();fault="sequence_calibration";}sequenceAt_=now;}
 }
 for(unsigned i=0;i<2;++i){
  auto& a=axes[i];
  if(i==1&&model==Model::G800DXA){a.wanted=0;a.tracking=false;}
  if(a.tracking&&(!a.calibrated||(a.wanted>0&&a.current>=a.target-0.5f)||(a.wanted<0&&a.current<=a.target+0.5f))){a.wanted=0;a.tracking=false;}
  if(a.calibrated&&((a.wanted<0&&a.current<=0)||(a.wanted>0&&a.current>=(i?180:maximum)))){a.wanted=0;a.tracking=false;}
  if(model==Model::G800DXA&&a.hasRun&&uint32_t(now-a.runAt)>=180000){
   stop();fault="continuous_motion_timeout";
  }
  output(i,a.wanted,now);
 }
 if(presetActive&&!moving()){presetActive=false;presetSlot=-1;}
}
bool Button::sample(bool released,uint32_t now){
 if(released!=raw_){raw_=released;changed_=now;}
 if(uint32_t(now-changed_)<40)return false;
 if(raw_){armed_=true;stable_=true;return false;}
 if(stable_&&armed_){stable_=false;armed_=false;return true;}
 stable_=false;return false;
}
bool number(const std::string& s,float& out){
 if(s.empty())return false;
 char* end=nullptr;errno=0;
 std::string value=s;std::replace(value.begin(),value.end(),',','.');
 out=std::strtof(value.c_str(),&end);
 if(end==value.c_str()||errno||!std::isfinite(out))return false;
 while(*end==' '||*end=='\r'||*end=='\n'||*end=='\t')++end;
 return *end==0;
}
float heading(float a){a=std::fmod(a,360.f);return a<0?a+360:a;}
float nearest(float angle,float current,float maximum,bool bi){
 float best=-1,distance=1e9;
 for(int d=0;d<(bi?2:1);++d)for(int turn=0;turn<2;++turn){
  float candidate=heading(angle+d*180)+turn*360;
  if(candidate<=maximum&&std::abs(candidate-current)<distance){best=candidate;distance=std::abs(candidate-current);}
 }
 return best;
}
} // namespace forkrot
