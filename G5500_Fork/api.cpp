#include "api.hpp"
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cctype>
namespace forkrot {
std::string jsonQuote(const std::string& s){std::string out="\"";for(unsigned char ch:s){if(ch=='"'||ch=='\\'){out+='\\';out+=ch;}else if(ch<32){char b[8];std::snprintf(b,sizeof b,"\\u%04x",ch);out+=b;}else out+=ch;}return out+'"';}
namespace {
std::string get(const Form& f,const char* key){auto it=f.find(key);return it==f.end()?"":it->second;}
bool has(const Form& f,const char* key){return f.find(key)!=f.end();}
bool integer(const std::string& s,unsigned& v,unsigned lo,unsigned hi){float n;if(!number(s,n)||std::floor(n)!=n||n<lo||n>hi)return false;v=unsigned(n);return true;}
bool boolean(const std::string& s,bool& v){if(s=="true"||s=="1"){v=true;return true;}if(s=="false"||s=="0"){v=false;return true;}return false;}
bool ip(const std::string& s){std::istringstream st(s);std::string part;int count=0;while(std::getline(st,part,'.')){unsigned v;if(!integer(part,v,0,255))return false;++count;}return count==4&&!s.empty()&&s.back()!='.';}
std::string targetJson(Target t){std::ostringstream o;o<<"{\"az\":"<<t.az<<",\"el\":"<<t.el<<",\"hasAz\":"<<(t.hasAz?"true":"false")<<",\"hasEl\":"<<(t.hasEl?"true":"false")<<'}';return o.str();}
}
std::string stateJson(const Controller& c,const Protocol& p,const Settings& cfg,uint32_t now){
 std::ostringstream o;o<<std::fixed<<std::setprecision(2);
 o<<"{\"model\":"<<jsonQuote(c.model==Model::G5500?"G5500":"G800DXA")<<",\"controlMode\":"<<jsonQuote(Controller::modeName(c.mode))
 <<",\"remainingMs\":"<<c.remaining(now)<<",\"pendingValid\":"<<(c.pendingValid?"true":"false")<<",\"pending\":"<<targetJson(c.pending)
 <<",\"presetActive\":"<<(c.presetActive?"true":"false")<<",\"presetSlot\":"<<c.presetSlot<<",\"speedEnabled\":"<<(c.speedEnabled?"true":"false")
 <<",\"speedPercent\":"<<c.speedPercent<<",\"fault\":"<<jsonQuote(c.fault)<<",\"rotorName\":"<<jsonQuote(p.rotorName)
 <<",\"n1mmPort\":"<<cfg.n1mmPort<<",\"pstPort\":"<<cfg.pstPort<<",\"reportPort\":"<<cfg.reportPort<<",\"reportHost\":"<<jsonQuote(cfg.reportHost)
 <<",\"n1mmReport\":"<<jsonQuote(p.positionReport(c.axes[0].current))
 <<",\"presets\":[";
 for(unsigned i=0;i<4;++i){if(i)o<<',';o<<"{\"slot\":"<<i+1<<",\"label\":"<<jsonQuote(c.presets[i].label)<<",\"target\":"<<targetJson(c.presets[i].target)<<",\"valid\":"<<(c.valid(c.presets[i].target)?"true":"false")<<'}';}
 o<<"]}";return o.str();
}
Response api(Controller& c,Protocol& p,Settings& cfg,const std::string& path,const Form& f,uint32_t now){
 Result r=Result::Invalid;
 if(path=="/api/mode"){
  auto m=get(f,"mode");if(m=="manual"||m=="auto"||m=="auto_1h")r=c.setMode(m=="manual"?Mode::Manual:m=="auto"?Mode::Auto:Mode::AutoHour,now);
 }else if(path=="/api/execute")r=c.execute(now);
 else if(path=="/api/preset"||path=="/api/preset/run"){
  unsigned slot;if(integer(get(f,"slot"),slot,1,4)){
   if(path=="/api/preset/run")r=c.preset(slot-1,now);
   else {float az,el=0;if(number(get(f,"az"),az)&&(c.model==Model::G800DXA||number(get(f,"el"),el)))r=c.savePreset(slot-1,get(f,"label"),{az,el,true,c.model==Model::G5500});}
  }
 }else if(path=="/api/settings"){
  auto next=cfg;auto name=p.rotorName;auto model=c.model;bool enabled=c.speedEnabled;unsigned speed=c.speedPercent;bool valid=true;
  if(has(f,"model")){auto m=get(f,"model");valid=m=="G5500"||m=="G800DXA";model=m=="G800DXA"?Model::G800DXA:Model::G5500;}
  if(has(f,"rotorName")){name=get(f,"rotorName");valid=valid&&!name.empty()&&name.size()<=31;for(unsigned char ch:name)valid=valid&&(std::isalnum(ch)||ch=='_'||ch=='-'||ch==' ');}
  if(has(f,"n1mmPort"))valid=integer(get(f,"n1mmPort"),next.n1mmPort,1,65535)&&valid;
  if(has(f,"pstPort"))valid=integer(get(f,"pstPort"),next.pstPort,1,65534)&&valid;
  if(has(f,"reportPort"))valid=integer(get(f,"reportPort"),next.reportPort,1,65535)&&valid;
  if(has(f,"reportHost")){next.reportHost=get(f,"reportHost");valid=valid&&(next.reportHost.empty()||ip(next.reportHost));}
  if(has(f,"speedEnabled"))valid=boolean(get(f,"speedEnabled"),enabled)&&valid;
  if(has(f,"speedPercent"))valid=integer(get(f,"speedPercent"),speed,25,100)&&valid;
  valid=valid&&next.n1mmPort!=next.pstPort;
  if(valid){if(model!=c.model){c.setModel(model);p.resetPst();}cfg=next;p.rotorName=name;c.speedEnabled=enabled;c.speedPercent=speed;r=Result::Ok;}
 }else if(path=="/api/target"){
  float az,el=0;if(number(get(f,"az"),az)&&(!has(f,"el")||number(get(f,"el"),el)))r=c.target({az,el,true,has(f,"el")},false,now);
 }else if(path=="/api/move"){
  auto d=get(f,"d");if(d=="S"){c.stop();r=Result::Ok;}
  else if(d=="A"||d=="E"){c.stop(d=="A"?0:1);r=Result::Ok;}
  else if(d=="L"||d=="R"||d=="U"||d=="D")r=c.jog(d=="U"||d=="D"?1:0,d=="R"||d=="U"?1:-1,false,now);
 }
 return {r==Result::Ok||r==Result::Pending?200:r==Result::Ignored?409:400,std::string("{\"ok\":")+(r==Result::Ok||r==Result::Pending?"true":"false")+",\"result\":"+jsonQuote(Controller::resultName(r))+"}"};
}
Form decodeForm(const std::string& input){
 auto decode=[](const std::string& s){std::string o;for(size_t i=0;i<s.size();++i){if(s[i]=='+')o+=' ';else if(s[i]=='%'&&i+2<s.size()&&std::isxdigit((unsigned char)s[i+1])&&std::isxdigit((unsigned char)s[i+2])){o+=char(std::strtol(s.substr(i+1,2).c_str(),nullptr,16));i+=2;}else o+=s[i];}return o;};
 Form f;std::istringstream st(input);std::string part;while(std::getline(st,part,'&')){auto pos=part.find('=');if(pos!=std::string::npos)f[decode(part.substr(0,pos))]=decode(part.substr(pos+1));}return f;
}
}
