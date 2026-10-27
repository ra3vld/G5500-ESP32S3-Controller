#include "panel_input.hpp"
#include "panel_command.hpp"
#include "touch_sample.hpp"
#include <cassert>
#include <cstdio>
#include <utility>
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
