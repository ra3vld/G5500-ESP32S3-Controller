#include "motion_fixture.hpp"
#include "api.hpp"
#include "calibration_record.hpp"
#include "crowpanel_knob.hpp"
#include <cassert>
#include <iostream>
using namespace forkrot;
void near(float a,float b) { assert(std::abs(a-b)<.002f); }
int main() {
  unsigned paths=0;
  assert(!AzRange::tenth(1.0001f) && !AzRange::tenth(.0001f));
  for (float value : {-3599.9f,-60.1f,.1f,1.1f,3599.9f}) assert(AzRange::tenth(value));
  for (AzRange r : {AzRange{0,360},{0,450},{-60,390},{50,500},{300,390},
                     {-700,-250},{-3600,-3599},{50,770},{-360,720},{3600,7200},{-60.1f,389.9f}}) {
    assert(r.valid());
    Controller c = forkrot::testController(); assert(c.setRange(r,AZ_CUSTOM)); c.feedback(r.start,0,true,true);
    assert(c.valid({r.start,0,true,false}) && c.valid({r.end,0,true,false}));
    assert(!c.valid({r.start-.1f,0,true,false}) && !c.valid({r.end+.1f,0,true,false}));
    for (float current : {r.start,r.end,(r.start+r.end)/2}) for (int angle=0;angle<360;angle+=7)
      for (bool opposite : {false,true}) {
        float best=NAN, distance=INFINITY;
        for (int k=-20;k<=30;++k) for (int o=0;o<=(opposite?1:0);++o) {
          const float v=heading(float(angle+180*o))+360*k;
          const float d=std::abs(v-current);
          if(r.contains(v)&&(d<distance||(d==distance&&v<best))){best=v;distance=d;}
        }
        const float actual=nearest(float(angle),current,r,opposite);
        if(std::isnan(best))assert(std::isnan(actual));else near(actual,best);
        ++paths;
      }
    c.jog(0,-1,false,0);c.tick(0);assert(c.axes[0].direction==0);
    c.feedback(r.end,0,true,true);c.jog(0,1,false,300);c.tick(300);assert(c.axes[0].direction==0);
    const auto revision=c.rangeRevision;assert(c.setRange(r,AZ_CUSTOM));assert(c.rangeRevision==revision);
    Controller copy=c;copy.minimum+=1;assert(c.minimum==r.start);
  }
  near(nearest(299.9f,-60.1f,AzRange{-60.1f,389.9f}),-60.1f);
  near(nearest(29.9f,389.9f,AzRange{-60.1f,389.9f}),389.9f);
  near(nearest(300,-50,AzRange{-60,390}),-60);
  near(nearest(20,60,AzRange{50,500}),380);
  near(nearest(0,180,AzRange{0,720}),0);
  near(nearest(300,-60,AzRange{-60,390}),-60);
  assert(std::isnan(nearest(90,320,AzRange{300,390})));
  for(AzRange bad : {AzRange{0,0},{0,3601},{-3601,0},{3601,4000},{NAN,450},{0,INFINITY},{.01f,450}})
    assert(!bad.valid());
  Controller c = forkrot::testController();c.feedback(60,20,true,true);assert(c.setRange({50,500},AZ_CUSTOM));
  Protocol p(c);Settings settings;
  assert(p.datagram("<PST><AZIMUTH>20</AZIMUTH></PST>",0).result==Result::Pending);near(c.pending.az,380);
  assert(p.datagram("<N1MMRotor><rotor>*</rotor><goazi>20</goazi></N1MMRotor>",1).result==Result::Pending);near(c.pending.az,380);
  assert(api(c,p,settings,"/api/target",{{"az","500"}},2).status==200);
  assert(c.savePreset(0,"End",{500,0,true,false})==Result::Ok);
  assert(p.line("M500",3).result==Result::Pending);
  assert(p.line("P 500 0",4).result==Result::Pending);
  assert(c.setRange({-60,390},AZ_CUSTOM));c.feedback(-50,20,true,true);
  assert(api(c,p,settings,"/api/target",{{"az","-60"}},5).status==200);
  assert(p.line("M-60",6).result==Result::Pending);
  CrowPanelKnob knob(c);knob.setBearing(300,7);near(c.axes[0].target,-60);
  c.stopNow(-1,8);c.feedback(10,20,true,true);knob.sample(true,false,-1,100,false);near(c.axes[0].target,-10);
  // Inaccessible PST command must leave the LP/base cache and the existing target untouched.
  c.setRange({300,390},AZ_CUSTOM);c.feedback(320,20,true,true);p.resetSession();
  assert(p.datagram("<PST><AZIMUTH>350</AZIMUTH></PST>",10).result==Result::Pending);
  assert(p.datagram("<PST><AZIMUTH>100</AZIMUTH></PST>",11).result==Result::Invalid);near(c.pending.az,350);
  assert(p.datagram("<PST><LP>1</LP></PST>",12).result==Result::Invalid);
  assert(p.datagram("<PST><LP>0</LP></PST>",13).result==Result::Ok);near(c.pending.az,350);
  // Changing geometry stops AZ immediately, leaves independent EL tracking, cancels pending commands.
  c.target({350,100,true,true},false,20);c.tick(320);assert(c.axes[1].direction==1);
  const auto revision=c.rangeRevision;c.setRange({50,500},AZ_CUSTOM,321);
  assert(c.axes[0].direction==0 && c.axes[1].direction==1 && !c.pendingValid);
  assert(api(c,p,settings,"/api/target",{{"az","300"},{"rangeRevision",std::to_string(revision)}},322).status==409);
  const auto before=c.rangeRevision;assert(!c.setRange({0,450},AZ_P36));assert(c.rangeRevision==before);
  c.setRange({0,1},AZ_CUSTOM);
#if DRIVE_SIMULATION
  assert(c.tolerance(0)==0);
#else
  near(c.tolerance(0),.1f);
#endif
  { // Presets retain their coordinate; every sequence point must remain reachable.
    Controller motion = forkrot::testController();motion.setRange({-60,390},AZ_CUSTOM);motion.feedback(-60,0,true,true);
    assert(motion.savePreset(0,"Negative",{-50,0,true,false})==Result::Ok);
    assert(motion.preset(0,0)==Result::Ok);motion.tick(0);assert(motion.axes[0].direction==1);
    motion.setRange({50,500},AZ_CUSTOM,1);
    assert(motion.presets[0].target.az==-50 && motion.preset(0,2)==Result::Invalid);
    motion.feedback(60,0,true,true);motion.setMode(Mode::Auto,3);
    const Target points[]={{380,0,true,false},{500,0,true,false}};
    assert(motion.sequence(points,2,1000,4)==Result::Ok);
    motion.tick(304);assert(motion.sequenceCount()==2 && motion.axes[0].direction==1);
    const auto rev=motion.rangeRevision;
    motion.setRange({50,500},AZ_CUSTOM,305);assert(motion.rangeRevision==rev && motion.axes[0].direction==1);
    motion.setRange({300,390},AZ_CUSTOM,306);assert(!motion.sequenceCount() && !motion.moving());
    assert(motion.sequence(points,2,1000,307)==Result::Invalid);
    motion.feedback(320,0,false,true);assert(motion.target({350,0,true,false},false,308)==Result::NotCalibrated);
    assert(motion.jog(0,1,false,309)==Result::Ok); // Calibration positioning is still available.
    motion.stopNow(-1,310);assert(!motion.moving());
  }
  for (bool reverse : {false,true}) {
    Controller controller = forkrot::testController();Calibration cal(controller);
    auto &s=cal.state();s.az45OSet=s.az45FSet=s.az45LinearValid=true;
    s.cal.az45_0=reverse?2800:100;s.cal.az45Max=reverse?100:2800;
    for(unsigned i=0;i<6;++i)s.az45CalMv[i]=s.cal.az45_0+(s.cal.az45Max-s.cal.az45_0)*int(i)/5;
    cal.refreshMultiCalValidity();
    assert(cal.configureAz(AZ_CUSTOM,-60,450));assert(cal.activeAzLinearValid());
    near(cal.mapAz(s.custom.zeroMv),-60);near(cal.mapAz(s.custom.fullMv),390);near(cal.mapAz(1450),165);
    s.calibrationMode=CAL_MULTI_POINT;assert(cal.activeAzMultiValid());near(cal.mapAz(1450),165);
    assert(cal.configureAz(AZ_CUSTOM,50,450));near(cal.mapAz(1450),275);assert(cal.activeAzMultiValid());
    assert(cal.configureAz(AZ_P36,0,360));assert(!cal.activeAzLinearValid());
    assert(cal.configureAz(AZ_CUSTOM,50,450));assert(cal.activeAzMultiValid());
    assert(cal.configureAz(AZ_CUSTOM,300,90));assert(!cal.activeAzLinearValid()&&!cal.activeAzMultiValid());
    assert(s.custom.count==2);
    assert(!cal.editAzPoint("add_capture",330));assert(s.custom.count==2);
    assert(cal.editAzPoint("add_capture",330,1450));assert(s.custom.millivolts[1]==1450);
    const auto captured=calibrationRecord(s);
    assert(!cal.editAzPoint("add_capture",330,2100));
    assert(!cal.editAzPoint("add_capture",300,100));assert(!cal.editAzPoint("add_capture",390,2800));
    assert(!cal.editAzPoint("add_capture",400,100));assert(!cal.editAzPoint("add_capture",330.01f,100));
    assert(calibrationRecord(s)==captured);
    CalibrationState capturedState;assert(readCalibrationRecord(captured,capturedState));
    assert(capturedState.custom.count==3 && capturedState.custom.degrees[1]==30 && capturedState.custom.millivolts[1]==1450);
    assert(!cal.editAzPoint("add",330));
    assert(!cal.editAzPoint("delete",300));assert(!cal.editAzPoint("delete",390));
    assert(!cal.editAzPoint("add",400));assert(!cal.editAzPoint("add",330.01f));
    assert(cal.editAzPoint("capture",300,reverse?2800:100));
    assert(cal.editAzPoint("capture",330,1450));assert(!cal.activeAzMultiValid());
    assert(cal.editAzPoint("capture",390,reverse?100:2800));assert(cal.activeAzMultiValid());
    near(cal.mapAz(1450),330);
    near(cal.mapAz(reverse?2125:775),315);near(cal.mapAz(reverse?775:2125),360);
    assert(cal.editAzPoint("capture",330,0));assert(!cal.activeAzMultiValid());
    assert(cal.editAzPoint("delete",330));assert(cal.activeAzMultiValid());
    assert(cal.editAzPoint("add_capture",301,0));assert(s.custom.millivolts[1]==0);
    for(int i=2;i<=14;++i)assert(cal.editAzPoint("add",300+i));
    assert(s.custom.millivolts[2]==-1); // Legacy add still creates an uncaptured point.
    assert(!cal.editAzPoint("add_capture",320,1000));
    assert(!cal.editAzPoint("add",320));assert(s.custom.count==16);
    const auto record=calibrationRecord(s);CalibrationState restored;
    assert(readCalibrationRecord(record,restored));assert(restored.custom.count==16);
    auto corrupt=record;corrupt[20]^=1;assert(!readCalibrationRecord(corrupt,restored));
  }
  std::cout<<"PASS: AZ range routing ("<<paths<<" cases), boundaries, interfaces, calibration, record validation\n";
}
