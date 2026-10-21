#include "motion_fixture.hpp"
#include "api.hpp"
#include "live.hpp"
#include <cassert>
#include <iostream>
using namespace forkrot;
struct Rig {
  Controller c=forkrot::testController();Protocol p{c};Settings settings;Hud h{c,p};
  Rig(){c.hardware.drive=DRIVE_YAESU;c.hardware.simulation=false;c.feedback(90,90,true,true);h.begin(1,10);}
  Form packet(unsigned serial,const char *d="H",unsigned mask=3,float az=1,float el=0,unsigned gesture=1){
    return {{"d",d},{"client","4294967295"},{"session","4294967294"},{"gesture",std::to_string(gesture)},
      {"serial",std::to_string(serial)},{"mask",std::to_string(mask)},{"az",std::to_string(az)},{"el",std::to_string(el)}};
  }
  Response send(const Form &form,uint32_t now){auto r=api(c,p,settings,"/api/move",form,now);c.tick(now);return r;}
};
int main(){
  // Renew for ten seconds on each axis/in each direction, then lose STOP entirely.
  for(unsigned axis:{0u,1u})for(float direction:{-1.f,1.f}){
    Rig r;unsigned seq=0;
    for(unsigned now=0;now<=10000;now+=200){
      assert(r.send(r.packet(++seq,"H",3,axis?0:direction,axis?direction:0),now).status==200);
      assert(r.c.axes[axis].direction==int(direction));
    }
    r.c.tick(10999);assert(r.c.axes[axis].direction==int(direction));
    r.c.tick(11000);assert(!r.c.axes[axis].direction&&!r.c.axes[axis].wanted);
    assert(r.send(r.packet(++seq,"H",3,axis?0:direction,axis?direction:0),11001).status==400);
    assert(!r.c.moving()); // Expired gestures cannot resume on a late renewal.
  }
  {
    Rig r;
    assert(r.send(r.packet(2,"S",3,0,0),0).status==200);
    assert(r.send(r.packet(1,"H",3,1,0),100).status==400);
    assert(!r.c.moving()); // Release can arrive before the first delayed press.
  }
  {
    Rig r;assert(r.send(r.packet(1,"H",3,1,-1),0).status==200);
    const auto late=r.packet(2,"H",3,1,-1);
    assert(r.send(r.packet(3,"S",3,0,0),200).status==200);assert(!r.c.moving());
    assert(r.send(late,300).status==400);assert(!r.c.moving());
    assert(r.send(r.packet(4,"H",3,-1,0,2),400).status==200);
    assert(r.send(r.packet(3,"S",3,0,0),450).status==400); // Old STOP cannot stop a newer press.
    assert(r.c.axes[0].wanted==-1);
  }
  {
    Rig r;assert(r.send(r.packet(1,"H",3,-1,1),0).status==200);
    const auto late=r.packet(2,"H",3,-1,1);
    assert(r.send(r.packet(3,"A",1,0,0),200).status==200);
    assert(!r.c.axes[0].direction&&r.c.axes[1].direction==1);
    assert(r.send(late,250).status==400);assert(!r.c.axes[0].direction);
    assert(r.send(r.packet(4,"H",3,0,1),400).status==200);
    assert(!r.c.axes[0].direction&&r.c.axes[1].direction==1);
    assert(r.send(r.packet(5,"E",2,0,0),500).status==200);assert(!r.c.moving());
    assert(r.send(r.packet(6,"H",3,1,0),600).status==200);assert(r.c.axes[0].wanted==1);
    assert(r.c.target({150,0,true,false},false,700)==Result::Ok);r.c.tick(700);
    assert(r.send(r.packet(7,"S",3,0,0),800).status==200);
    assert(r.c.axes[0].tracking&&r.c.axes[0].target==150); // Ownership protects subsequent numeric motion.
  }
  {
    Rig r;auto bad=r.packet(1);bad.erase("serial");assert(r.send(bad,0).status==400);assert(!r.c.moving());
    bad=r.packet(1);bad["client"]="4294967296";assert(r.send(bad,0).status==400);
    bad=r.packet(1);bad["d"]="L";assert(r.send(bad,0).status==400); // No fallback to an unlimited jog.
    r.c.setModel(Model::G800DXA);r.c.feedback(90,0,true,false);
    assert(r.send(r.packet(1,"H",1,1,0),0).status==200);
    assert(r.send(r.packet(2,"S",1,0,0),10).status==200);assert(!r.c.moving());
  }
  {
    Rig r;LiveRequest request;
    const std::string prefix="v=1&boot=a&selection=0&path=/api/move&";
    assert(request.parse(prefix+"cid=1&d=H&client=5&session=6&gesture=1&serial=1&mask=3&az=1&el=0"));
    assert(executeLive(request,r.c,r.p,r.settings,r.h,0).status==200);assert(r.c.axes[0].direction==1);
    assert(request.parse(prefix+"cid=3&d=S&client=5&session=6&gesture=1&serial=3&mask=3&az=0&el=0"));
    assert(request.stop());assert(executeLive(request,r.c,r.p,r.settings,r.h,10).status==200);assert(!r.c.moving());
    assert(request.parse(prefix+"cid=2&d=H&client=5&session=6&gesture=1&serial=2&mask=3&az=1&el=0"));
    assert(executeLive(request,r.c,r.p,r.settings,r.h,20).status==400);assert(!r.c.moving());
  }
  std::cout<<"PASS: keyboard API lease/long hold/lost STOP, stale packet barriers, independent axes, ownership and live transport\n";
}
