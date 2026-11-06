#include "panel_presentation.hpp"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace forkrot;
using panel::Presentation;
static PanelView view() {
  PanelView v;v.hud=true;v.sourceId=12;v.dualAxis=true;
  v.feedback=v.secondaryFeedback=v.targetValid=v.secondaryTargetValid=true;
  v.current=v.target=90;v.secondaryCurrent=v.secondaryTarget=40;
  v.tolerance=v.secondaryTolerance=.5f;
  return v;
}
int main() {
  auto v=view();Presentation p;
  assert(!p.update(v,0).reached);
  assert(!p.update(v,499).reached);assert(p.update(v,500).reached);
  // Real sensor values and the drive tolerance stay untouched. Alternate around
  // both rounding and arrival boundaries for a minute, including short spikes.
  for(uint32_t t=600;t<=60600;t+=100) {
    v.current=90.5f+(t%200?.18f:-.18f);
    if(t%3000==0)v.current=92;
    const auto raw=v;
    const auto s=p.update(v,t);
    assert(v==raw);assert(s.current.degrees==90);assert(s.reached);
    assert(s.current.position<90.7f);
  }
  // Meaningful stationary displacement is eventually visible, rather than frozen.
  v.current=100;p.update(v,61000);
  assert(p.update(v,62499).reached);assert(!p.update(v,62500).reached);
  const auto displaced=p.update(v,71000);assert(displaced.current.degrees==100);
  assert(displaced.current.position>99.8f);
  // No added smoothing lag during movement, including multi-turn coordinates.
  v.movingNow=true;v.current=721.25f;v.secondaryCurrent=-91;
  auto s=p.update(v,71100);assert(s.current.position==v.current && s.current.degrees==721);
  assert(s.secondary.position==-91 && !s.reached);
  v.movingNow=false;v.azDirection=1;v.current=722;s=p.update(v,71200);
  assert(s.current.position==722);v.azDirection=0;
  // Axis selection preserves the two independent filters and the goal latch.
  v=view();v.sourceId=34;p.update(v,72000);assert(p.update(v,72500).reached);
  std::swap(v.current,v.secondaryCurrent);std::swap(v.target,v.secondaryTarget);
  std::swap(v.tolerance,v.secondaryTolerance);v.elevation=true;
  s=p.update(v,72600);assert(s.current.degrees==40 && s.secondary.degrees==90 && s.reached);
  // New goal, changed tolerance, source switch and lost/invalid feedback reset.
  v.target+=5;assert(!p.update(v,72700).reached);
  v.target=v.current;p.update(v,72800);assert(p.update(v,73300).reached);
  v.tolerance=.25;assert(!p.update(v,73400).reached);assert(p.update(v,73900).reached);
  v.sourceId=56;v.current=200;s=p.update(v,74000);assert(!s.reached && s.current.position==200);
  v.target=200;p.update(v,74100);assert(p.update(v,74600).reached);
  v.feedback=false;assert(!p.update(v,74700).reached);
  v.feedback=true;v.current=300;assert(p.update(v,74800).current.position==300);
  v.current=std::numeric_limits<float>::quiet_NaN();assert(!p.update(v,74900).reached);
  v.current=200;assert(p.update(v,75000).current.position==200);
  v.secondaryFeedback=false;assert(!p.update(v,76000).reached);
  v.secondaryFeedback=true;v.secondaryTargetValid=false;p.update(v,76100);
  assert(p.update(v,76600).reached);
  v.targetValid=false;assert(!p.update(v,76700).reached);
  // Enter after a real in-tolerance sample, even if noise keeps crossing that
  // boundary afterwards; reject the same noise if the goal was never reached.
  v=view();v.current=90.7f;Presentation arrival;
  assert(!arrival.update(v,0).reached);assert(!arrival.update(v,1000).reached);
  v.current=90.4f;arrival.update(v,1100);
  v.current=90.7f;assert(!arrival.update(v,1599).reached);assert(arrival.update(v,1600).reached);
  v.pending=true;assert(arrival.update(v,1700).reached);
  v.target=100;assert(!arrival.update(v,1800).reached);
  // Filter response does not depend on refresh cadence and timers survive wrap.
  Presentation fast,slow;v=view();fast.update(v,0);slow.update(v,0);v.current=100;
  Presentation::Result a,b;
  for(uint32_t t=20;t<=2000;t+=20)a=fast.update(v,t);
  b=slow.update(v,2000);assert(std::abs(a.current.position-b.current.position)<.11f);
  Presentation wrap;v=view();const uint32_t start=std::numeric_limits<uint32_t>::max()-200;
  wrap.update(v,start);assert(!wrap.update(v,start+499).reached);assert(wrap.update(v,start+500).reached);
  v.current=92;wrap.update(v,start+600);assert(!wrap.update(v,start+2100).reached);
  std::puts("PASS: idle noise/rounding, arrival hysteresis and dwell, sustained displacement, motion, independent axes, source/goal/tolerance/feedback changes, NaN, cadence and clock wrap");
}
