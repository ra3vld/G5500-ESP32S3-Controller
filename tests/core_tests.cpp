#include "api.hpp"
#include "input.hpp"
#include <iostream>
#include <stdexcept>
#include <cmath>
using namespace forkrot;
unsigned checks=0;
#define CHECK(expr) do {++checks;if(!(expr))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #expr);}while(0)
Controller ready(Model model=Model::G5500){Controller c;c.model=model;c.feedback(20,30,true,true);return c;}
Controller rampReady(){auto c=ready(Model::G800DXA);c.speedEnabled=true;c.decelerationMs=1000;return c;}
std::string n1(float a,const std::string& name="G5500",int offset=0,int bi=0){return "<N1MMRotor><rotor>"+name+"</rotor><goazi>"+std::to_string(a)+"</goazi><offset>"+std::to_string(offset)+"</offset><bidirectional>"+std::to_string(bi)+"</bidirectional></N1MMRotor>";}
int main(){try{
 {auto c=ready();Protocol p(c);Settings cfg;c.feedback(20,30,true,false);
 const auto response=api(c,p,cfg,"/api/target",{{"az","90"},{"el","45"}},0);
 CHECK(response.status==400);CHECK(response.body.find("not_calibrated")!=std::string::npos);
 CHECK(!c.moving()&&!c.targetKnown);CHECK(c.axes[0].target==0&&c.axes[1].target==0);
 CHECK(api(c,p,cfg,"/api/target",{{"az","90"}},1).status==200);
 CHECK(c.axes[0].tracking&&!c.axes[1].tracking);}
 {auto c=ready();Protocol p(c);Settings cfg;
 CHECK(!c.lastTarget.hasAz&&!c.lastTarget.hasEl);
 CHECK(c.target({170,0,true,false},true,0)==Result::Pending);CHECK(!c.lastTarget.hasAz);
 CHECK(c.execute(1)==Result::Ok);c.tick(1);CHECK(c.moving());
 CHECK(c.lastTarget.az==170&&c.lastTarget.hasAz&&!c.lastTarget.hasEl);
 c.target({250,0,true,false},true,2);c.stop();c.tick(3);
 CHECK(!c.moving()&&!c.pendingValid);CHECK(c.lastTarget.az==170);
 CHECK(stateJson(c,p,cfg,3).find("\"lastTarget\":{\"az\":170,\"el\":0,\"hasAz\":true,\"hasEl\":false}")!=std::string::npos);
 CHECK(stateJson(c,p,cfg,3).find("\"moving\":false")!=std::string::npos);
 Controller copy=c;CHECK(copy.lastTarget.az==170&&copy.lastTarget.hasAz&&!copy.lastTarget.hasEl);
 CHECK(c.target(c.lastTarget,false,4)==Result::Ok);c.tick(304);CHECK(c.moving());
 c.feedback(170,30,true,true);c.tick(305);CHECK(!c.moving());CHECK(c.lastTarget.az==170);
 CHECK(c.target({600,0,true,false},false,306)==Result::Invalid);CHECK(c.lastTarget.az==170);
 c.setMaximum(360);CHECK(!c.lastTarget.hasAz);
 c.target({90,45,true,true},false,307);c.setModel(Model::G800DXA);
 CHECK(!c.lastTarget.hasAz&&!c.lastTarget.hasEl);}
 {auto c=ready();Protocol p(c);Settings cfg;
 CHECK(api(c,p,cfg,"/api/target",{{"el","70"}},0).status==200);c.tick(0);
 CHECK(c.lastTarget.hasEl&&!c.lastTarget.hasAz&&c.lastTarget.el==70);
 CHECK(c.axes[0].direction==0&&c.axes[1].direction==1);
 c.stop();c.tick(1);CHECK(c.lastTarget.el==70);
 for(const auto& form:{Form{},Form{{"az",""}},Form{{"el","bad"}},Form{{"el","181"}}})
   CHECK(api(c,p,cfg,"/api/target",form,2).status==400);
 CHECK(c.lastTarget.el==70&&!c.moving());}
 {auto c=rampReady();c.jog(0,1,true,0);c.tick(0);CHECK(c.axes[0].speedOutputPercent==25);
 c.tick(0);CHECK(c.axes[0].speedOutputPercent==25);c.tick(500);CHECK(c.axes[0].speedOutputPercent==37.5f);
 c.tick(1000);CHECK(c.axes[0].speedOutputPercent==50);c.tick(1200);CHECK(c.axes[0].speedOutputPercent==50);
 c.speedPercent=100;c.tick(1700);CHECK(c.axes[0].speedOutputPercent==75);c.tick(2200);CHECK(c.axes[0].speedOutputPercent==100);
 c.speedPercent=25;c.tick(2700);CHECK(c.axes[0].speedOutputPercent==87.5f);c.tick(5200);CHECK(c.axes[0].speedOutputPercent==25);
 c.stop();c.tick(5201);CHECK(c.axes[0].speedOutputPercent==0);CHECK(!c.moving());}
 {auto c=rampReady();uint32_t now=0xffffff00u;c.jog(0,1,true,now);c.tick(now);
 c.tick(now+200);c.tick(now+700);c.tick(now+1000);CHECK(c.axes[0].speedOutputPercent==50);}
 {auto c=rampReady();c.jog(0,1,true,0);c.tick(0);c.tick(1000);c.jog(0,-1,true,1000);c.tick(1000);
 CHECK(c.axes[0].direction==1);c.tick(1250);CHECK(c.axes[0].speedOutputPercent==37.5f);CHECK(c.axes[0].direction==1);
 c.tick(2000);CHECK(c.axes[0].speedOutputPercent==0);CHECK(c.axes[0].direction==0);
 c.tick(2299);CHECK(c.axes[0].direction==0);c.tick(2300);CHECK(c.axes[0].direction==-1);CHECK(c.axes[0].speedOutputPercent==25);
 c.tick(2800);CHECK(c.axes[0].speedOutputPercent==37.5f);CHECK(c.axes[0].runAt==0);}
 {auto c=rampReady();c.target({90,0,true,false},false,0);c.tick(0);c.tick(1000);CHECK(c.axes[0].speedOutputPercent==50);
 c.feedback(80,0,true,false);c.tick(1250);CHECK(c.axes[0].speedOutputPercent==37.5f);
 c.feedback(84.5,0,true,false);c.tick(1500);CHECK(c.axes[0].speedOutputPercent==26.25f);
 c.feedback(89.5,0,true,false);c.tick(1501);CHECK(!c.moving());CHECK(c.axes[0].speedOutputPercent==0);
 c.startSpeedPercent=100;c.target({83,0,true,false},false,1801);c.tick(1801);CHECK(c.axes[0].direction==-1);CHECK(c.axes[0].speedOutputPercent<=29);
 c.feedback(83.5,0,true,false);c.tick(1802);CHECK(!c.moving());}
 {auto c=rampReady();c.jog(0,1,true,0);c.tick(0);c.tick(1000);c.jog(0,-1,true,1000);
 c.feedback(450,0,true,false);c.tick(1100);CHECK(c.axes[0].direction==0);CHECK(c.axes[0].wanted==-1);CHECK(c.axes[0].speedOutputPercent==0);
 c.jog(0,-1,true,1400);c.tick(1400);CHECK(c.axes[0].direction==-1);c.tick(181400);CHECK(!c.moving());CHECK(c.fault=="continuous_motion_timeout");CHECK(c.axes[0].speedOutputPercent==0);}
 {auto c=rampReady();c.preset(1,0);c.tick(0);c.tick(1000);CHECK(c.presetActive);
 c.feedback(20,0,false,false);c.tick(1001);CHECK(!c.moving());CHECK(!c.presetActive);CHECK(c.axes[0].speedOutputPercent==0);}
 for(auto packet:{"S","A","stop"}){auto c=rampReady();Protocol p(c);c.jog(0,1,true,0);c.tick(0);c.tick(1000);
 CHECK(p.line(packet,1001).result==Result::Ok);c.tick(1001);CHECK(!c.moving());CHECK(c.axes[0].speedOutputPercent==0);}
 {auto c=rampReady();Protocol p(c);Settings cfg;
 CHECK(api(c,p,cfg,"/api/settings",{{"startSpeedPercent","30"},{"approachSpeedPercent","35"},{"accelerationMs","3000"},{"decelerationMs","2500"},{"slowdownDegrees","15"}},0).status==200);
 CHECK(c.startSpeedPercent==30);CHECK(c.approachSpeedPercent==35);CHECK(c.accelerationMs==3000);CHECK(c.decelerationMs==2500);CHECK(c.slowdownDegrees==15);
 for(const auto& entry:Form{{"startSpeedPercent","24"},{"approachSpeedPercent","101"},{"accelerationMs","99"},{"decelerationMs","10001"},{"slowdownDegrees","0"}}){
  const auto before=stateJson(c,p,cfg,0);CHECK(api(c,p,cfg,"/api/settings",{{entry.first,entry.second},{"speedPercent","100"}},0).status==400);CHECK(stateJson(c,p,cfg,0)==before);
 }
 c.jog(0,1,true,0);c.tick(0);CHECK(api(c,p,cfg,"/api/settings",{{"speedEnabled","false"}},1).status==200);c.tick(1);CHECK(!c.moving());CHECK(c.axes[0].speedOutputPercent==0);
 c.setModel(Model::G5500);c.speedEnabled=true;c.jog(0,1,true,2);c.tick(302);CHECK(c.axes[0].direction==1);CHECK(c.axes[0].speedOutputPercent==0);}
 {auto c=ready();Protocol p(c);CHECK(p.datagram("<PST><AZIMUTH>450</AZIMUTH></PST>",0).result==Result::Pending);CHECK(c.pending.az==90);CHECK(p.line("M450",1).result==Result::Pending);CHECK(c.pending.az==450);}
 {auto c=ready();Protocol p(c);CHECK(p.positionReport(137)=="G5500@1370");CHECK(p.positionReport(270)=="G5500@2700");CHECK(p.positionReport(450)=="G5500@900");CHECK(p.positionReport(-1)=="G5500@3590");CHECK(p.positionReport(359.96f)=="G5500@0");}
 {auto c=ready();CHECK(c.mode==Mode::Manual);CHECK(c.target({90,0,true,false},true,0)==Result::Pending);CHECK(c.pendingValid);CHECK(!c.moving());
 CHECK(c.jog(0,-1,true,1)==Result::Ok);c.tick(1);CHECK(c.axes[0].direction==-1);CHECK(c.pending.az==90);
 CHECK(c.execute(2)==Result::Ok);c.tick(2);CHECK(c.axes[0].direction==0);c.tick(302);CHECK(c.axes[0].direction==1);
 CHECK(!c.pendingValid);c.feedback(90,30,true,true);c.tick(303);CHECK(!c.moving());}
 {auto c=ready();c.setMode(Mode::Auto,0);CHECK(c.target({90,0,true,false},true,0)==Result::Ok);c.tick(0);
 c.setMode(Mode::Manual,1);CHECK(c.axes[0].direction==1);c.target({180,0,true,false},true,2);CHECK(c.axes[0].target==90);CHECK(c.pending.az==180);
 c.stop();c.tick(3);CHECK(!c.moving());CHECK(!c.pendingValid);CHECK(c.mode==Mode::Manual);
 c.setMode(Mode::Auto,4);c.stop();CHECK(c.mode==Mode::Auto);CHECK(c.target({180,0,true,false},true,5)==Result::Ok);}
 {auto c=ready();c.target({90,0,true,false},true,10);CHECK(c.setMode(Mode::AutoHour,10)==Result::Ok);c.tick(10);CHECK(c.remaining(10)==3600000);
 c.tick(3600010);CHECK(c.mode==Mode::Manual);CHECK(c.axes[0].target==90);CHECK(c.axes[0].direction==1);
 CHECK(c.target({180,0,true,false},true,3600010)==Result::Pending);CHECK(c.axes[0].target==90);
 c.setMode(Mode::AutoHour,3600011);CHECK(c.axes[0].target==180);CHECK(c.remaining(3600011)==3600000);}
 {auto c=ready();c.setMode(Mode::AutoHour,0);CHECK(c.target({90,0,true,false},true,3600000)==Result::Pending);CHECK(c.mode==Mode::Manual);CHECK(!c.moving());}
 {auto c=ready();uint32_t start=0xfffffff0u;c.setMode(Mode::AutoHour,start);c.tick(start+3599999u);CHECK(c.mode==Mode::AutoHour);c.tick(start+3600000u);CHECK(c.mode==Mode::Manual);
 c.restoreMode(Mode::AutoHour);CHECK(c.mode==Mode::Manual);c.restoreMode(Mode::Auto);CHECK(c.mode==Mode::Auto);CHECK(!c.pendingValid);}
 {auto c=ready();Protocol p(c);c.setMode(Mode::AutoHour,0);CHECK(c.preset(1,0)==Result::Ok);c.tick(0);CHECK(c.presetActive);
 CHECK(c.target({270,0,true,false},true,1)==Result::Ignored);CHECK(c.jog(0,-1,true,1)==Result::Ignored);CHECK(!c.pendingValid);
 auto pkt=p.datagram("<PST><LP>1</LP><AZIMUTH>180</AZIMUTH></PST>",1);CHECK(pkt.result==Result::Ignored);
 CHECK(c.preset(2,2)==Result::Ok);CHECK(c.axes[0].target==180);c.tick(3600000);CHECK(c.mode==Mode::Manual);CHECK(c.presetActive);
 c.feedback(180,0,true,true);c.tick(3600001);CHECK(!c.presetActive);CHECK(!c.pendingValid);
 CHECK(p.datagram("<PST><AZIMUTH>30</AZIMUTH></PST>",3600002).result==Result::Pending);CHECK(c.pending.az==30);}
 {auto c=ready();c.preset(1,0);c.tick(0);Protocol p(c);CHECK(p.datagram("<PST><STOPR>1</STOPR></PST>",1).result==Result::Ok);c.tick(1);CHECK(!c.presetActive);CHECK(!c.moving());}
 {auto c=ready();c.feedback(20,30,false,true);CHECK(c.target({90,0,true,false},true,0)==Result::Pending);CHECK(c.execute(1)==Result::NotCalibrated);CHECK(c.pendingValid);
 CHECK(c.preset(1,2)==Result::NotCalibrated);CHECK(c.jog(0,1,true,3)==Result::Ok);}
 {auto c=ready();c.feedback(20,30,false,true);c.target({90,0,true,false},true,0);CHECK(c.setMode(Mode::Auto,1)==Result::Ok);CHECK(c.mode==Mode::Auto);CHECK(c.pendingValid);CHECK(c.fault=="target_not_calibrated");CHECK(!c.targetKnown);}
 {auto c=ready(Model::G800DXA);CHECK(c.target({90,1,true,true},true,0)==Result::Invalid);CHECK(c.target({90,0,true,true},true,0)==Result::Pending);
 CHECK(c.execute(1)==Result::Ok);CHECK(c.jog(1,1,true,1)==Result::Invalid);c.tick(1);CHECK(c.axes[1].direction==0);c.tick(180001);CHECK(!c.moving());CHECK(c.fault=="continuous_motion_timeout");}
 {auto c=ready();c.jog(0,1,true,0);c.tick(0);c.jog(0,-1,true,10);c.tick(10);CHECK(c.axes[0].direction==0);c.tick(309);CHECK(c.axes[0].direction==0);c.tick(310);CHECK(c.axes[0].direction==-1);
 c.feedback(0,30,true,true);c.tick(311);CHECK(c.axes[0].direction==0);}
 {auto c=ready();CHECK(c.target({NAN,0,true,false},true,0)==Result::Invalid);CHECK(c.target({451,0,true,false},true,0)==Result::Invalid);
 c.setMaximum(360);CHECK(c.target({450,0,true,false},true,0)==Result::Invalid);CHECK(c.valid(c.presets[3].target));}
 {auto c=ready();Protocol p(c);CHECK(p.datagram(n1(180),0).result==Result::Pending);CHECK(c.pending.az==180);
 CHECK(p.datagram(n1(90,"OTHER"),1).result==Result::Ignored);CHECK(c.pending.az==180);
 CHECK(p.datagram(n1(90,"OTHER, G5500"),2).result==Result::Pending);CHECK(c.pending.az==90);
 CHECK(p.datagram(n1(90,"G5500",30),3).result==Result::Pending);CHECK(c.pending.az==120);
 c.feedback(340,0,true,true);CHECK(p.datagram(n1(10),4).result==Result::Pending);CHECK(c.pending.az==370);
 c.feedback(170,0,true,true);p.datagram(n1(0,"G5500",0,1),5);CHECK(c.pending.az==180);
 CHECK(p.datagram("<N1MMRotor><stop><rotor>G5500</rotor><freqband>14</freqband></stop></N1MMRotor>",6).result==Result::Ok);CHECK(!c.pendingValid);}
 // Captured from N1MM Logger+ 1.0.11455, Turn Rotor with no selected antenna.
 {auto c=ready();Protocol p(c);
 CHECK(p.datagram("<N1MMRotor><rotor>*</rotor><goazi>90.0</goazi><offset>0.0</offset><bidirectional>0</bidirectional><freqband>14.0</freqband></N1MMRotor>",0).result==Result::Pending);
 CHECK(c.pending.az==90);CHECK(c.axes[0].direction==0);
 CHECK(p.datagram(n1(180,"*OTHER"),1).result==Result::Ignored);CHECK(c.pending.az==90);
 CHECK(p.datagram("<N1MMRotor><stop><rotor>*</rotor><freqband>14.0</freqband></stop></N1MMRotor>",2).result==Result::Ok);CHECK(!c.pendingValid);}
 {auto c=ready();Protocol p(c);auto original=c.axes[0].target;
 for(auto s:{"<PST><AZIMUTH>90</AZIMUTH>","<PST><AZIMUTH>90x</AZIMUTH></PST>","<PST><AZIMUTH>NaN</AZIMUTH></PST>","<PST><AZIMUTH>90</AZIMUTH><AZIMUTH>30</AZIMUTH></PST>","<PST><AZIMUTH>90</AZIMUTH><BAD>1</BAD></PST>","<PST><LEFT>1</LEFT><RIGHT>1</RIGHT></PST>","<PST><AZIMUTH><BAD>90</BAD></AZIMUTH></PST>","<!DOCTYPE PST><PST></PST>"}){CHECK(p.datagram(s,0).result==Result::Invalid);CHECK(!c.pendingValid);CHECK(c.axes[0].target==original);}
 CHECK(p.datagram(std::string(4097,'x'),0).result==Result::Invalid);}
 {auto c=ready();Protocol p(c);c.jog(0,1,true,0);c.tick(0);CHECK(p.datagram("<N1MMRotor><stop><rotor>G5500</rotor><BAD>1</BAD></stop></N1MMRotor>",1).result==Result::Invalid);CHECK(c.axes[0].wanted==1);}
 {auto c=ready();Protocol p(c);CHECK(p.datagram("<PST><AZIMUTH>128</AZIMUTH></PST>",0).result==Result::Pending);CHECK(c.pending.az==128);
 CHECK(p.datagram("<PST><LP>1</LP></PST>",1).result==Result::Pending);CHECK(c.pending.az==308);
 CHECK(p.datagram("<PST><LP>0</LP></PST>",2).result==Result::Pending);CHECK(c.pending.az==128);
 CHECK(p.datagram("<PST><LEFT>1</LEFT></PST>",3).result==Result::Ok);c.tick(3);CHECK(c.axes[0].direction==-1);CHECK(c.pending.az==128);
 CHECK(p.datagram("<PST>AZ?</PST>",4).reply=="AZ:20.0\r");CHECK(p.datagram("<PST>TGA?</PST>",4).reply=="TGA:128.0\r");
 CHECK(p.datagram("<PST><CALL>RA3VLD</CALL><FREQUENCY>14000</FREQUENCY></PST>",5).result==Result::Ok);CHECK(c.pending.az==128);
 p.datagram("<PST><AZIMUTH>90</AZIMUTH><STOP>1</STOP></PST>",6);CHECK(!c.pendingValid);}
 {auto c=ready();Protocol p(c);c.savePreset(0,"Park",{45,10,true,true});
 CHECK(p.datagram("<PST><PARK>1</PARK></PST>",0).result==Result::Pending);CHECK(c.pending.az==45);CHECK(c.pending.el==10);
 CHECK(p.datagram("<PST><PARK>bad</PARK></PST>",1).result==Result::Invalid);CHECK(c.pending.az==45);
 c.setMode(Mode::Auto,2);CHECK(p.datagram("<PST><PARK>1</PARK></PST>",3).result==Result::Ok);CHECK(c.axes[0].target==45);
 c.preset(1,4);CHECK(p.datagram("<PST><PARK>1</PARK></PST>",5).result==Result::Ignored);CHECK(c.axes[0].target==90);CHECK(!c.pendingValid);}
 {auto c=ready();Protocol p(c);CHECK(p.line("M090",0).result==Result::Pending);CHECK(c.pending.az==90);
 CHECK(p.line("R",1).result==Result::Ok);c.tick(1);CHECK(c.axes[0].direction==1);CHECK(p.line("P 123,5 45,5",2).result==Result::Pending);CHECK(c.pending.az==123.5f);
 CHECK(p.line("W 100 bad",3).result==Result::Invalid);CHECK(c.pending.az==123.5f);
 CHECK(p.line("SET_POS 456 0",3).result==Result::Invalid);CHECK(c.pending.az==123.5f);
 CHECK(p.line("p",4).reply=="20.000000\n30.000000\n");p.line("X2",4);CHECK(c.speedPercent==50);
 p.line("S",5);c.tick(5);CHECK(!c.moving());CHECK(!c.pendingValid);}
 {auto c=ready();Protocol p(c);CHECK(p.line("M 1 90 180",0).result==Result::Pending);CHECK(p.line("T",1).result==Result::Pending);CHECK(c.execute(2)==Result::Ok);
 c.tick(2);CHECK(c.axes[0].target==90);c.tick(1002);CHECK(c.axes[0].target==180);
 c.stop();c.tick(2002);CHECK(!c.moving());
 CHECK(p.line("T",2003).result==Result::Invalid);CHECK(!c.pendingValid);
 c.setMode(Mode::AutoHour,0);p.line("M 999 90 180",0);p.line("T",0);c.tick(3600000);CHECK(c.mode==Mode::Manual);CHECK(c.axes[0].target==90);}
 {Button b;CHECK(!b.sample(false,0));CHECK(!b.sample(false,100));CHECK(!b.sample(true,101));CHECK(!b.sample(true,141));CHECK(!b.sample(false,142));CHECK(b.sample(false,182));CHECK(!b.sample(false,500));
 CHECK(!b.sample(true,501));CHECK(!b.sample(false,510));CHECK(!b.sample(false,550));CHECK(!b.sample(true,551));CHECK(!b.sample(true,591));CHECK(!b.sample(false,592));CHECK(b.sample(false,632));}
 {LineBuffer b;std::string line;bool emitted=false;for(char byte:std::string(513,'x')+"R")emitted=b.feed(byte,line)||emitted;CHECK(!emitted);CHECK(!b.feed('\r',line));CHECK(!b.feed('\n',line));emitted=false;for(char byte:std::string("M090"))emitted=b.feed(byte,line)||emitted;CHECK(!emitted);CHECK(b.feed('\r',line));CHECK(line=="M090");CHECK(!b.feed('\n',line));CHECK(!b.feed('\0',line));CHECK(!b.feed('R',line));CHECK(!b.feed('\n',line));}
 {auto c=ready();Protocol p(c);Settings cfg;auto response=api(c,p,cfg,"/api/settings",{{"model","G800DXA"},{"speedPercent","500"}},0);CHECK(response.status==400);CHECK(c.model==Model::G5500);
 CHECK(api(c,p,cfg,"/api/settings",{{"model","G800DXA"},{"speedPercent","75"},{"speedEnabled","true"}},1).status==200);CHECK(c.model==Model::G800DXA);CHECK(c.speedPercent==75);CHECK(c.speedEnabled);
 CHECK(api(c,p,cfg,"/api/settings",{{"n1mmPort","1234"}},2).status==400);CHECK(cfg.n1mmPort==12040);
 CHECK(api(c,p,cfg,"/api/preset",{{"slot","1"},{"label","Россия"},{"az","270"}},3).status==200);CHECK(c.presets[0].label=="Россия");CHECK(!c.presets[0].target.hasEl);
 CHECK(api(c,p,cfg,"/api/preset",{{"slot","0"},{"label","bad"},{"az","0"}},3).status==400);
 CHECK(jsonQuote("a\"b\nc")=="\"a\\\"b\\u000ac\"");CHECK(stateJson(c,p,cfg,4).find("Россия")!=std::string::npos);}
 std::cout<<"PASS: "<<checks<<" assertions covering modes, presets, protocols, limits, button, profiles, API\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
