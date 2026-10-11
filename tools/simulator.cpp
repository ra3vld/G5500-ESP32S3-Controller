#include "api.hpp"
#include <iostream>
#include <sstream>
#include <chrono>
#include <cmath>
using namespace forkrot;
int main(){
 Controller c;Protocol p(c);Settings cfg;
 std::array<std::array<Preset,4>,2> profiles{{c.presets,c.presets}};
 for(auto& preset:profiles[1])preset.target.hasEl=false;
 c.feedback(0,0,true,true);
 uint32_t now=0;float az=0,el=0;Button button;
 std::string line;
 while(std::getline(std::cin,line)){
  std::string reply;Response response;std::string kind;
  auto split=line.find(' ');kind=line.substr(0,split);auto args=split==std::string::npos?"":line.substr(split+1);
  if(kind=="step"){
   unsigned ms=std::stoul(args);float seconds=ms/1000.f;
   az=std::max(0.f,std::min(c.maximum,az+c.axes[0].direction*6.f*seconds));
   el=std::max(0.f,std::min(180.f,el+c.axes[1].direction*6.f*seconds));
   if(c.axes[0].tracking){if(c.axes[0].direction>0)az=std::min(az,c.axes[0].target);else if(c.axes[0].direction<0)az=std::max(az,c.axes[0].target);}
   if(c.axes[1].tracking){if(c.axes[1].direction>0)el=std::min(el,c.axes[1].target);else if(c.axes[1].direction<0)el=std::max(el,c.axes[1].target);}
   c.feedback(az,el,true,true);now+=ms;c.tick(now);
  }else if(kind=="udp"||kind=="line"){
   auto packet=decodeForm(args)["packet"];
   auto result=kind=="udp"?p.datagram(packet,now):p.line(packet,now,true);
   response.status=result.result==Result::Invalid?400:200;
   reply=result.reply;response.body=std::string("{\"result\":")+jsonQuote(Controller::resultName(result.result))+",\"matched\":"+(result.matched?"true":"false")+"}";
  }else if(kind=="button"){
   if(button.sample(args=="released",now))c.execute(now);
  }else if(kind=="http"){
   auto pos=args.find(' ');std::string path=args.substr(0,pos);auto form=decodeForm(pos==std::string::npos?"":args.substr(pos+1));
   auto old=c.model;auto oldIndex=old==Model::G5500?0:1;profiles[oldIndex]=c.presets;
   if(path=="/api/azmode"){
    float v;if(number(form["m"],v)&&(v==36||v==45))c.setMaximum(v==36?360:450);else response.status=400;
   }else if(path=="/api/calmode"||path=="/api/cal"||path=="/api/cal2")response.body="{\"ok\":true}";
   else response=api(c,p,cfg,path,form,now);
   if(old!=c.model){c.presets=profiles[c.model==Model::G5500?0:1];c.feedback(az,el,true,true);}
  }
  c.tick(now);
  auto state=stateJson(c,p,cfg,now);state.pop_back();
  std::ostringstream extra;extra<<",\"az\":"<<az<<",\"el\":"<<(c.model==Model::G800DXA?0:el)<<",\"targetAz\":"<<c.axes[0].target<<",\"targetEl\":"<<c.axes[1].target<<",\"targetValid\":"<<(c.targetKnown?"true":"false")<<",\"azMode\":"<<(c.maximum==360?36:45)
   <<",\"directionAz\":"<<c.axes[0].direction<<",\"directionEl\":"<<c.axes[1].direction<<",\"wantedAz\":"<<c.axes[0].wanted
   <<",\"calibrated\":true,\"selectedCalOk\":true,\"calMode\":\"2\",\"ip\":\"SIMULATOR\",\"wifiMode\":\"Виртуальное оборудование\",\"ssid\":\"\",\"rssi\":null,\"btConnected\":false,\"motion\":"<<jsonQuote(c.moving()?"MOVING":"STOP");
  for(auto key:{"az2p0","az2pf","el2p0","el2pf","azp0","azp90","azp180","azp270","azp360","azp450","elp0","elp45","elp90","elp135","elp180"})extra<<",\""<<key<<"\":0";
  state+=extra.str()+"}";
  std::cout<<"{\"status\":"<<response.status<<",\"body\":"<<jsonQuote(response.body)<<",\"reply\":"<<jsonQuote(reply)<<",\"state\":"<<state<<"}"<<std::endl;
 }
}
