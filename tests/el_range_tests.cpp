#include "motion_fixture.hpp"
#include "api.hpp"
#include "calibration_record.hpp"
#include "crowpanel_knob.hpp"
#include <cassert>
#include <iostream>
using namespace forkrot;
static void near(float a,float b) { assert(std::abs(a-b)<.002f); }
int main() {
  for (RotationRange r : {RotationRange{0,180},{-30,210},{50,230},{-180,-30},
                          {10,11},{-3600,0},{3600,7200},{-.1f,179.9f}}) {
    Controller c = forkrot::testController();Protocol p(c);Settings settings;
    assert(c.setElRange(r,EL_CUSTOM));c.feedback(20,r.start,true,true);
    assert(c.valid({0,r.start,false,true}) && c.valid({0,r.end,false,true}));
    assert(!c.valid({0,r.start-.1f,false,true}) && !c.valid({0,r.end+.1f,false,true}));
    assert(api(c,p,settings,"/api/target",{{"el",std::to_string(r.end)}},1).status==200);
    c.stopNow(-1,2);c.jog(1,-1,false,3);c.tick(3);assert(!c.axes[1].direction);
    c.feedback(20,r.end,true,true);c.jog(1,1,false,4);c.tick(4);assert(!c.axes[1].direction);
    assert(c.savePreset(0,"EL stop",{0,r.end,false,true})==Result::Ok);
    const auto rev=c.rangeRevision;c.setElRange(r,EL_CUSTOM,5);assert(c.rangeRevision==rev);
    Controller copy=c;assert(copy.elRange==r && copy.elProfile==EL_CUSTOM);
    c.feedback(20,r.start,true,true);CrowPanelKnob knob(c);knob.selectAxis();
    knob.sample(true,false,1,100,false);near(c.axes[1].target,std::min(r.end,r.start+20));
    if(r.span()<359) {knob.setBearing(r.end+.1f,101);assert(knob.lastResult==Result::Invalid);}
    c.stopNow(-1,102);
  }
  Controller c = forkrot::testController();Protocol p(c);Settings settings;
  assert(c.setElRange({-30,210},EL_CUSTOM));c.feedback(20,0,true,true);
  CrowPanelKnob knob(c);knob.selectAxis();knob.setBearing(340,0);near(c.axes[1].target,-20);c.stopNow(-1,0);
  assert(p.line("W100 -20",1).result==Result::Pending);near(c.pending.el,-20);
  assert(p.line("P 100 200",2).result==Result::Pending);near(c.pending.el,200);
  assert(p.datagram("<PST><ELEVATION>-20</ELEVATION></PST>",3).result==Result::Pending);
  const auto saved=c.pending;assert(p.datagram("<PST><ELEVATION>211</ELEVATION></PST>",4).result==Result::Invalid);
  near(c.pending.el,saved.el);
  c.target({300,150,true,true},false,10);c.tick(310);assert(c.axes[0].direction && c.axes[1].direction);
  const auto rev=c.rangeRevision;c.setElRange({-50,190},EL_CUSTOM,311);
  assert(c.axes[0].direction && !c.axes[1].direction && !c.pendingValid);
  assert(api(c,p,settings,"/api/target",{{"el","100"},{"rangeRevision",std::to_string(rev)}},312).status==409);
  assert(api(c,p,settings,"/api/move",{{"d","S"},{"rangeRevision",std::to_string(rev)}},313).status==200);
  c.setElRange({0,1},EL_CUSTOM,314);
#if DRIVE_SIMULATION
  assert(c.tolerance(1)==0);
#else
  near(c.tolerance(1),.1f);
#endif
  c.feedback(0,0,true,true);const Target points[]={{0,.2f,false,true},{0,.9f,false,true}};
  c.setMode(Mode::Auto,315);assert(c.sequence(points,2,1000,316)==Result::Ok);
  c.setElRange({-10,-1},EL_CUSTOM,317);assert(!c.sequenceCount());
  assert(c.sequence(points,2,1000,318)==Result::Invalid);
  c.feedback(0,-10,true,false);assert(c.target({0,-5,false,true},false,319)==Result::NotCalibrated);
  assert(c.jog(1,1,false,320)==Result::Ok);c.stopNow(-1,321);
  c.setModel(Model::G800DXA);c.feedback(0,0,true,false);
  assert(c.elProfile==EL_STANDARD && (c.elRange==RotationRange{0,180}));
  assert(c.valid({100,0,true,true}) && !c.valid({100,1,true,true}));
  for (bool reverse : {false,true}) {
    Controller controller = forkrot::testController();Calibration cal(controller);auto &s=cal.state();
    s.elOSet=s.elFSet=s.elLinearValid=true;s.cal.el0=reverse?2800:100;s.cal.elMax=reverse?100:2800;
    for(unsigned i=0;i<EL_CAL_N;++i)s.elCalMv[i]=s.cal.el0+(s.cal.elMax-s.cal.el0)*int(i)/4;
    cal.refreshMultiCalValidity();assert(cal.configureEl(EL_CUSTOM,-30,180));
    assert(cal.activeElLinearValid());near(cal.mapEl(s.cal.el0),-30);near(cal.mapEl(s.cal.elMax),150);
    s.calibrationMode=CAL_MULTI_POINT;assert(cal.activeElMultiValid());near(cal.mapEl(1450),60);
    assert(cal.configureEl(EL_CUSTOM,20,180));near(cal.mapEl(1450),110);
    assert(cal.configureEl(EL_CUSTOM,-30,240));assert(!cal.activeElLinearValid()&&!cal.activeElMultiValid());
    assert(!cal.editElPoint("add_capture",0));assert(s.customEl.count==2);
    assert(cal.editElPoint("add_capture",0,1450));assert(s.customEl.millivolts[1]==1450);
    const auto captured=calibrationRecord(s);
    assert(!cal.editElPoint("add_capture",0,2100));
    assert(!cal.editElPoint("add_capture",-30,100));assert(!cal.editElPoint("add_capture",210,2800));
    assert(!cal.editElPoint("add_capture",220,100));assert(!cal.editElPoint("add_capture",.01f,100));
    assert(calibrationRecord(s)==captured);
    CalibrationState capturedState;assert(readCalibrationRecord(captured,capturedState));
    assert(capturedState.customEl.count==3 && capturedState.customEl.degrees[1]==30 && capturedState.customEl.millivolts[1]==1450);
    assert(!cal.editElPoint("add",0));assert(!cal.editElPoint("add",.01f));
    assert(!cal.editElPoint("delete",-30) && !cal.editElPoint("delete",210));
    assert(cal.editElPoint("capture",-30,reverse?2800:100));
    assert(cal.editElPoint("capture",0,1450));assert(!cal.activeElMultiValid());
    assert(cal.editElPoint("capture",210,reverse?100:2800));assert(cal.activeElMultiValid());
    near(cal.mapEl(reverse?2125:775),-15);near(cal.mapEl(reverse?775:2125),105);
    assert(cal.editElPoint("capture",0,0));assert(!cal.activeElMultiValid());
    assert(cal.editElPoint("delete",0));assert(cal.activeElMultiValid());
    assert(cal.editElPoint("add_capture",1,0));assert(s.customEl.millivolts[1]==0);
    for(int i=2;i<=14;++i)assert(cal.editElPoint("add",float(i)));
    assert(s.customEl.millivolts[2]==-1);
    assert(!cal.editElPoint("add_capture",20,1000));
    assert(!cal.editElPoint("add",20));
    CalibrationState restored;assert(readCalibrationRecord(calibrationRecord(s),restored));
    assert(restored.customEl.count==16 && restored.elProfile==EL_CUSTOM);
    assert(cal.configureEl(EL_STANDARD,0,180));assert(cal.activeElMultiValid());near(cal.mapEl(1450),90);
    assert(!cal.configureEl(EL_STANDARD,1,180));assert(!cal.configureEl(EL_CUSTOM,0,3601));
    assert(!cal.configureEl(EL_CUSTOM,NAN,180));assert(!cal.configureEl(EL_CUSTOM,0,1.0001f));
    controller.model=Model::G800DXA;assert(!cal.configureEl(EL_CUSTOM,0,180));
  }
  std::cout<<"PASS: EL ranges, commands, stops, independent AZ, encoder, sequences, both calibration polarities, profiles and records\n";
}
