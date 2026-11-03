#include "panel_input.hpp"
#include "panel_command.hpp"
#include "touch_sample.hpp"
#include <cassert>
#include <cstdio>
#include <utility>
#include <limits>
using namespace forkrot;
using namespace forkrot::panel;

static TouchLayout layout() {
  TouchLayout l;
  l.mode={112,45,80,28};l.axis={55,50,45,20};l.dualAxis=true;
  l.current={45,90,145,48};l.target={50,140,145,24};
  l.arrows[0]={10,101,30,30};l.arrows[1]={200,101,30,30};
  l.stopArea={80,167,80,28};
  l.centerX=l.centerY=120;l.innerRadius=70;l.outerRadius=120;
  l.width=l.height=240;
  l.swipeDeadzone=6;l.swipeFull=50;
  return l;
}
int main() {
  {
    auto l=layout();l.menuAccess=true;TouchGesture hold;
    hold.sample(true,120,110,l,100);
    assert(hold.sample(true,120,110,l,3100).action==TouchAction::None);
    assert(hold.sample(true,120,110,l,3101).action==TouchAction::MenuOpen);
  }
  {
    for(bool online:{true,false})for(bool left:{false,true}) {
      auto l=layout();l.menuAccess=true;l.enabled=online;
      l.secondaryCurrent={uint16_t(left?50:145),100,40,28};
      const unsigned x=l.secondaryCurrent.x+20,y=114;
      TouchGesture tap;
      auto e=tap.sample(true,x,y,l,100);assert(e.action==TouchAction::None);
      e=tap.sample(false,0,0,l,200);assert(e.action==(online?TouchAction::Axis:TouchAction::None));
      if(online)assert(e.manual&&e.release&&!e.mask);
      // A drag that returns to its origin remains a swipe after release.
      tap.sample(true,x,y,l,1000);
      e=tap.sample(true,x+25,y-20,l,1100);
      if(online)assert(e.manual&&e.absoluteSpeed&&e.az>0&&e.el>0);
      e=tap.sample(true,x,y,l,1200);assert(e.action==TouchAction::None);
      assert(tap.sample(false,0,0,l,1300).action==TouchAction::None);
      // Both current values also admit the menu hold, including offline.
      tap.sample(true,x,y,l,2000);
      assert(tap.sample(true,x,y,l,5000).action==TouchAction::None);
      e=tap.sample(true,x,y,l,5001);assert(e.action==TouchAction::MenuOpen);
      assert(tap.sample(false,0,0,l,5010).action==TouchAction::None);
      // A context change cannot switch the newly selected axis on release.
      tap.sample(true,x,y,l,6000);++l.actionEpoch;
      assert(tap.sample(false,0,0,l,6100).action==TouchAction::None);
    }
    auto l=layout();l.menuAccess=true;l.secondaryCurrent={145,100,40,28};
    TouchGesture primary;
    primary.sample(true,100,110,l,100);assert(primary.sample(false,0,0,l,150).action==TouchAction::None);
    primary.sample(true,100,110,l,200);assert(primary.sample(false,0,0,l,250).action==TouchAction::SpeedOpen);
    l.dualAxis=false;TouchGesture single;
    single.sample(true,165,114,l,100);assert(single.sample(false,0,0,l,200).action==TouchAction::None);
  }
  {
    auto l=layout();l.menuAccess=true;l.targetSwipeDeadzone=12;
    l.menuTarget={38,120,169,52};
    TouchGesture hold;
    // Only the free padding captures a settings hold. The current value,
    // arrows, mode, axis, HUD name and STOP keep their existing actions.
    assert(l.menuTargetHit(45,145));assert(!l.target.contains(45,145));
    hold.sample(true,45,145,l,100);
    assert(hold.sample(false,0,0,l,200).action==TouchAction::None);
    // Drifting into the original goal rectangle must not confirm either.
    hold.sample(true,48,145,l,300);hold.sample(true,52,145,l,350);
    assert(hold.sample(false,0,0,l,400).action==TouchAction::None);
    hold.sample(true,45,145,l,1000);
    assert(hold.sample(true,45,145,l,4000).action==TouchAction::None);
    assert(hold.sample(true,45,145,l,4001).action==TouchAction::MenuOpen);
    assert(hold.sample(false,0,0,l,4010).action==TouchAction::None);
    l.menuTarget={0,0,240,240};
    for(const auto r:{l.current,l.arrows[0],l.arrows[1],l.mode,l.axis,l.name})
      assert(!l.menuTargetHit(r.x+r.width/2,r.y+r.height/2));
    l.stop=l.stopArea;assert(!l.menuTargetHit(120,180));
    assert(hold.sample(true,120,180,l,5000).action==TouchAction::Stop);
  }
  {
    for(bool online:{false,true}) {
      auto l=layout();l.menuAccess=true;l.targetSwipeDeadzone=12;l.enabled=online;l.confirmAllowed=online;
      TouchGesture hold;
      hold.sample(true,120,160,l,100);
      auto event=hold.sample(true,128,168,l,200); // Drift outside the label, within the captured hold.
      assert(!event.manual && event.action==TouchAction::None);
      event=hold.sample(true,112,168,l,3100);assert(event.action==TouchAction::None&&!event.manual);
      event=hold.sample(true,128,168,l,3101);assert(event.action==TouchAction::MenuOpen&&!event.manual);
      assert(hold.sample(true,120,160,l,5000).action==TouchAction::None);
      assert(hold.sample(false,0,0,l,5010).action==TouchAction::None);
      // The extra hold tolerance must never loosen short-tap confirmation.
      hold.sample(true,120,150,l,6000);hold.sample(true,128,150,l,6100);
      assert(hold.sample(false,0,0,l,6200).action==TouchAction::None);
      // Crossing the larger threshold captures a swipe (or cancels an offline hold).
      hold.sample(true,120,150,l,7000);
      assert(!hold.sample(true,132,150,l,7100).manual);
      event=hold.sample(true,133,150,l,7200);assert(event.manual==online);
      if(online)assert(event.az>0 && event.absoluteSpeed && !event.release);
      event=hold.sample(true,120,150,l,11000);assert(event.action==TouchAction::None);
      event=hold.sample(false,0,0,l,11010);assert(event.release==online);
    }
  }
  {
    auto l=layout();l.menuAccess=true;TouchGesture input;
    const auto start=std::numeric_limits<uint32_t>::max()-1500;
    input.sample(true,120,150,l,start);
    assert(input.sample(true,120,150,l,start+3000u).action==TouchAction::None);
    assert(input.sample(true,120,150,l,start+3001u).action==TouchAction::MenuOpen);
    assert(input.sample(true,120,150,l,start+9000u).action==TouchAction::None);
    assert(input.sample(false,0,0,l,start+9100u).action==TouchAction::None);
    // A short release still confirms. Offline/invalid targets still open settings.
    input.sample(true,120,150,l,10000);assert(input.sample(false,0,0,l,10010).action==TouchAction::Confirm);
    l.enabled=false;l.confirmAllowed=false;
    input.sample(true,120,150,l,11000);
    assert(input.sample(true,120,150,l,14001).action==TouchAction::MenuOpen);
    input.sample(false,0,0,l,14010);
    l.enabled=true;l.confirmAllowed=true;
    input.sample(true,120,150,l,15000);
    assert(input.sample(true,130,150,l,15100).manual);
    assert(input.sample(true,120,150,l,19000).action==TouchAction::None);
    assert(input.sample(false,0,0,l,19010).release);
    // Touch faults and page changes cannot turn an old contact into a menu command.
    input.sample(true,120,150,l,20000);l.menuAccess=false;l.enabled=false;
    assert(input.sample(true,120,150,l,24000).action!=TouchAction::MenuOpen);
    input.cancel();input.sample(false,0,0,l,24010);l.menuAccess=true;
    input.sample(false,0,0,l,24020);
    l.menuOpen=true;l.enabled=false;l.menuEpoch=1;l.menuButtons[0]={90,190,60,28};
    input.sample(true,120,200,l,25000);++l.menuEpoch;
    assert(input.sample(false,0,0,l,25100).action==TouchAction::None);
    input.sample(true,120,200,l,26000);
    assert(input.sample(false,0,0,l,26100).action==TouchAction::MenuBack);
    assert(input.sample(true,120,110,l,27000).action==TouchAction::None);
    assert(!input.sample(true,180,110,l,28000).manual);input.sample(false,0,0,l,28010);
    l.stop=l.stopArea;
    assert(input.sample(true,120,180,l,29000).action==TouchAction::Stop);
  }
  {
    TouchLayout r;r.width=480;r.height=320;r.centerX=240;r.centerY=160;
    r.innerRadius=93;r.outerRadius=160+DialTouchExtension;r.name={};
    assert(r.dialHit(450,160)&&!r.dialContains(451,160)&&!r.dialContains(480,160));
    TouchGesture input;input.sample(false,0,0,r,0);
    assert(input.sample(true,450,160,r,10).action==TouchAction::Target);
    assert(r.bearing(450,160)==90);input.sample(false,0,0,r,20);
    r.mode={360,130,60,44};
    assert(r.dialContains(380,150)&&!r.dialHit(380,150));
    assert(input.sample(true,380,150,r,30).action==TouchAction::Mode);input.sample(false,0,0,r,40);
    assert(input.sample(true,450,160,r,50).action==TouchAction::Target);
    // An established ring drag keeps its role over other buttons.
    assert(input.sample(true,380,150,r,60).action==TouchAction::Target);input.sample(false,0,0,r,70);
    r.stop={430,140,40,40};assert(!r.dialHit(450,160));
    assert(input.sample(true,450,160,r,80).action==TouchAction::Stop);
    input.sample(false,0,0,r,90);
    r=layout();r.outerRadius=120+DialTouchExtension;r.name={};
    for(auto point:{std::pair<int,int>{0,0},{239,0},{0,239},{239,239}}) {
      assert(r.dialHit(point.first,point.second));TouchGesture corner;
      assert(corner.sample(true,point.first,point.second,r,100).action==TouchAction::Target);
    }
  }
  auto l=layout();TouchGesture input;input.cancel();
  auto sample=[&](bool down,int x=0,int y=0){return input.sample(down,x,y,l,100);};
  // A finger held during boot/recovery must be released before it can confirm.
  assert(sample(true,120,150).action==TouchAction::None);
  sample(false);sample(true,120,150);
  assert(sample(false).action==TouchAction::Confirm);
  // A pending target that appears beneath a held contact cannot start motion.
  l.confirmAllowed=false;sample(true,120,150);l.confirmAllowed=true;
  assert(sample(false).action==TouchAction::None);
  sample(true,120,150);l.confirmAllowed=false;sample(true,120,150);l.confirmAllowed=true;
  assert(sample(false).action==TouchAction::None);
  // Replacement and reversal back to the original goal still invalidate a tap.
  sample(true,120,150);++l.actionEpoch;sample(true,120,150);++l.actionEpoch;
  assert(sample(false).action==TouchAction::None);
  sample(true,120,150);assert(sample(false).action==TouchAction::Confirm);
  auto e=sample(true,25,115);assert(e.manual && e.az==-1 && !e.release);
  const auto gesture=e.gesture;
  assert(sample(true,25,115).gesture==gesture);
  l.enabled=false;e=sample(false);assert(e.manual && e.release);
  l.enabled=true;assert(!sample(true,25,115).manual);
  sample(false);assert(sample(true,25,115).manual);
  l.stop=l.stopArea;e=sample(true,120,180);
  assert(e.action==TouchAction::None && e.manual && !e.release && e.az==-1);
  assert(sample(false).release);
  assert(sample(true,120,180).action==TouchAction::Stop);
  sample(false);l.enabled=false;input.cancel();
  assert(sample(true,120,180).action==TouchAction::Stop);
  // STOP does not turn into confirmation when motion stops before release.
  l.stop={};l.enabled=true;assert(sample(false).action==TouchAction::None);
  assert(sample(true,80,58).action==TouchAction::Axis);sample(false);
  assert(sample(true,150,58).action==TouchAction::Mode);sample(false);
  assert(sample(true,234,120).action==TouchAction::Target);
  assert(l.bearing(234,120)==90 && l.bearing(120,234)==180);sample(false);
  // Captured swipes preserve axis order and reach full speed at native sizes.
  sample(true,100,110);e=sample(true,150,60);
  assert(e.manual && e.absoluteSpeed && e.az==1 && e.el==1 && e.mask==3);
  e=sample(false);assert(e.manual && e.release && e.mask==3);

  // A moving STOP button cannot steal an established downward swipe, including
  // one that started on a target. Lifting and pressing STOP still stops at once.
  for(bool targetStart:{false,true}) {
    l=layout();
    const int startY=targetStart?150:110;
    sample(true,120,startY);e=sample(true,120,startY+10);
    assert(e.manual && e.absoluteSpeed && !e.release && e.el<0);
    const auto captured=e.gesture;
    l.stop={80,uint16_t(startY),80,28};
    e=sample(true,120,startY+10);
    assert(e.action==TouchAction::None && e.manual && !e.release && e.gesture==captured);
    l.stop=l.stopArea;
    for(int y:{170,180,190,230,180,80}) {
      e=sample(true,120,y);
      assert(e.action==TouchAction::None && e.manual && !e.release && e.gesture==captured);
      assert(y>startY?e.el<0:e.el>0);
    }
    e=sample(false);assert(e.manual && e.release && e.mask==2);
    assert(sample(true,120,180).action==TouchAction::Stop);sample(false);
  }

  PanelCommandContext shown;shown.target={120,40,true,true};shown.pending=true;
  auto current=shown;assert(current==shown);
  current.target.az=121;assert(!(current==shown) && current.sameScope(shown));
  current=shown;++current.selectionEpoch;assert(!current.sameScope(shown));
  current=shown;++current.rangeRevision;assert(!current.sameScope(shown));
  current=shown;current.axis=1;assert(!current.sameScope(shown));
  current=shown;current.mode=Mode::Auto;assert(!current.sameScope(shown));
  current=shown;current.driveError=true;assert(!(current==shown));

  uint8_t data[]{1,0x80,239,0,120};
  auto t=decodeCstTouch(data,240,240,42);
  assert(t.healthy && t.down && t.x==239 && t.y==120 && t.sampledAt==42);
  data[2]=240;t=decodeCstTouch(data,240,240,43);assert(!t.healthy && !t.down);
  t=decodeCstTouch(data,480,480,44);assert(t.healthy && t.down && t.x==240);
  data[0]=2;assert(!decodeCstTouch(data,480,480,45).healthy);
  data[0]=0;data[1]=data[2]=data[3]=data[4]=255;t=decodeCstTouch(data,240,240,46);
  assert(t.healthy && !t.down);
  data[0]=1;data[1]=0x40;data[2]=10;data[3]=0;data[4]=20;
  t=decodeCstTouch(data,240,240,47);assert(t.healthy && !t.down);
  std::puts("PASS: shared panel startup, stale confirmation, STOP, release/recovery, context and CST coordinates");
}
