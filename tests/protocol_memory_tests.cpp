#include "motion_fixture.hpp"
#include "protocol.hpp"
#include "input.hpp"
#include "packet_queue.hpp"
#include "http_request_gate.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <new>
static bool deny = false;
void *operator new(size_t n) { if (deny) throw std::bad_alloc(); if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc(); }
void *operator new[](size_t n) { return ::operator new(n); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p,size_t) noexcept { std::free(p); }
void operator delete[](void *p,size_t) noexcept { std::free(p); }
using namespace forkrot;
int main() {
  Controller c = forkrot::testController(); Protocol p(c); c.feedback(20,30,true,true); c.setMode(Mode::Auto,0);
  Controller stopController = forkrot::testController(); stopController.feedback(20,30,true,true);
  Controller regulated = forkrot::testController(); regulated.hardware.drive=DRIVE_BTS7960;
  for(auto &m:regulated.motionSettings) m.mode=RegulatorMode::Cascade;
  regulated.feedbackAt(0,20,true,0);regulated.feedbackAt(1,30,true,0);
  const std::string huge="<N1MMRotor><rotor>G5500</rotor><app>"+std::string(3800,'x')+"</app><goazi>180</goazi></N1MMRotor>";
  std::string many="M";while(many.size()<510)many+=" 1";
  LineBuffer input; PacketQueue<1024,8> queue;
  char out[512]; size_t length=0;uint32_t tag=0;std::string_view line;
  const std::string payload(512,'x');
  deny=true;
  for(unsigned at=20;at<=2000;at+=20) {
    regulated.feedbackAt(0,20+at*.001f,true,at);regulated.feedbackAt(1,30+at*.001f,true,at);
    assert(regulated.target({50+at*.01f,60+at*.01f,true,true},false,at)==Result::Ok);
    regulated.tick(at);
    assert(regulated.axes[0].motionError==MotionError::None&&regulated.axes[1].motionError==MotionError::None);
  }
  stopController.jog(0,1,false,0);stopController.jog(1,1,false,0);stopController.tick(0);
  assert(stopController.axes[0].direction==1 && stopController.axes[1].direction==1);
  stopController.stopNow(0,10);
  assert(stopController.axes[0].direction==0 && stopController.axes[1].direction==1);
  stopController.jog(0,-1,false,11);stopController.tick(11);
  assert(stopController.axes[0].direction==0);
  stopController.tick(310);assert(stopController.axes[0].direction==-1);
  stopController.stopNow(-1,311);
  assert(stopController.axes[0].direction==0 && stopController.axes[1].direction==0);
  float n;
  assert(number(" +12,5 ",n) && n==12.5f);
  for (const char *bad:{"+-2","--2","1x","NaN","inf","1e999"}) assert(!number(bad,n));
  for(unsigned i=0;i<10000;++i) {
    assert(p.datagram(huge,i).result==Result::Ok);
    assert(p.line("W001 10 20 30 40",i).result==Result::Ok);
    assert(p.line("T",i).result==Result::Ok);
    assert(p.line("S",i).result==Result::Ok);
    assert(p.line(many,i).result==Result::Invalid);
    for(char ch:std::string_view("M090\r")) if(input.feed(ch,line)) assert(line=="M090");
    assert(queue.push(payload,7));assert(queue.push(payload,8));assert(!queue.push("x",9));
    assert(queue.pop(out,sizeof out,length,tag) && length==512 && tag==7);
    assert(queue.pop(out,sizeof out,length,tag) && tag==8 && queue.empty());
  }
  const float target=c.axes[0].target;
  assert(p.datagram("<PST><AZIMUTH>10</AZIMUTH><AZIMUTH>20</AZIMUTH></PST>",1).result==Result::Invalid);
  assert(p.datagram("<PST><AZIMUTH>10</AZIMUTH>x</PST>",1).result==Result::Invalid);
  assert(c.axes[0].target==target);
  assert(queue.push("discard",1));queue.clear();assert(queue.empty());assert(queue.push("fresh",2));
  assert(queue.pop(out,sizeof out,length,tag) && tag==2);
  size_t args=0;
  assert(boundedForm("ssid=%D0%A2%D0%B5%D1%81%D1%82&pass=abc%26def",args) && args==2);
  args=0;assert(!boundedForm("ssid=bad%00name",args));
  args=0;assert(!boundedForm("x=%xx",args));
  input.feed('M', line);
  input.cancelPending();
  assert(!input.feed('R', line) && !input.feed('\n', line));
  assert(!input.feed('C', line) && input.feed('\n', line) && line == "C");
  deny=false;
  std::cout<<"PASS: two-axis Ruckig/PID replanning and 10000 XML/sequence/line/byte-queue cycles with C++ allocations forbidden\n";
}
