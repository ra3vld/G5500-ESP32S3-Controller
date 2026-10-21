#include "motion_fixture.hpp"
#include "live.hpp"
#include "hud_uart_tx.hpp"
#include <cassert>
#include <iostream>
#include <vector>
using namespace forkrot;
int main() {
  Controller c = forkrot::testController();Protocol p(c);Settings settings;Hud h(c,p);h.begin(1,10);c.feedback(20,30,true,true);
  LiveRequest request;
  const std::string prefix="v=1&cid=1&boot=000000000000000a&selection=0&path=";
  assert(request.parse(prefix+"%2Fapi%2Ftarget&az=123"));
  assert(executeLive(request,c,p,settings,h,0).status==200);assert(c.axes[0].target==123);
  assert(request.parse(prefix+"/api/move&d=S"));assert(request.stop());
  assert(executeLive(request,c,p,settings,h,1).status==200);assert(!c.moving());
  c.hardware.drive=DRIVE_BTS7960;c.hardware.simulation=false;
  const auto speedFingerprint=liveControlFingerprint(c,h),stopEpoch=c.stopEpoch;
  assert(request.parse(prefix+"/api/speed&axis=az&value=35"));
  assert(executeLive(request,c,p,settings,h,2).status==200);
  assert(c.runtimeSpeed(0)==35&&c.axes[0].target==123&&!c.moving()&&c.stopEpoch==stopEpoch);
  assert(speedFingerprint!=liveControlFingerprint(c,h));
  assert(!request.parse(prefix+"/api/target&az=123&az=456"));
  assert(!request.parse(prefix+"/api/target&az=%00"));
  assert(!request.parse(prefix+"/api/target&az=%f"));
  assert(!request.parse(prefix+"/api/settings&model=G800DXA"));
  assert(!request.parse(prefix+"/api/target&extra="+std::string(512,'a')));
  assert(request.parse("v=1&cid=1&boot=b&selection=0&path=/api/target&az=99"));
  assert(executeLive(request,c,p,settings,h,2).status==409);assert(c.axes[0].target==123);
  assert(request.parse("v=1&cid=1&boot=a&selection=1&path=/api/move&d=S"));
  assert(executeLive(request,c,p,settings,h,2).status==409);
  char data[LIVE_STATE_CAPACITY];JsonWriter writer(data,sizeof data);
  assert(writeLiveState(writer,c,h,3));assert(writer.size()<LIVE_STATE_CAPACITY-100);
  // Worst bounded labels/faults must fit together with the websocket envelope.
  h.config.role=DeviceRole::Hud;h.begin(1,10);
  const std::string escaped(48,'"');auto *peer=h.add(2,{2,4534,false},escaped.c_str());
  assert(peer&&h.select(2));c.fault=std::string(63,'"');
  h.selectionEpoch=h.rejected=h.timeouts=h.busy=UINT32_MAX;
  c.stopEpoch=UINT32_MAX;c.mode=Mode::AutoHour;c.pending={450,180,true,true};c.lastTarget=c.pending;
  for(auto &axis:c.axes){axis.current=axis.target=449.99f;axis.direction=-1;axis.speedOutputPercent=100;}
  JsonWriter worst(data,sizeof data);assert(writeLiveState(worst,c,h,3));
  assert(worst.size()<LIVE_STATE_CAPACITY-110);
  std::cout<<"Worst live state: "<<worst.size()<<" bytes before envelope\n";
  auto fingerprint=liveControlFingerprint(c,h);c.axes[0].current+=1;
  assert(fingerprint==liveControlFingerprint(c,h));c.axes[0].target+=1;
  assert(fingerprint!=liveControlFingerprint(c,h));
  // Preserve sub-centidegree speed limits/setpoints in the live projection.
  c.motionSettings[0].mode=RegulatorMode::Cascade;c.motionSettings[0].velocity=.001f;
  JsonWriter tiny(data,sizeof data);assert(writeLiveState(tiny,c,h,3));
  assert(std::string(tiny.data()).find("\"speedSetpoint\":0.001")!=std::string::npos);
  assert(std::string(tiny.data()).find("\"speedLimit\":0.001")!=std::string::npos);
  HudUartTx uart;uint8_t telemetry[60]={1,2,3,4,8},ack[80]={1,2,3,4,10},command[70]={1,2,3,4,9};
  assert(uart.enqueue(telemetry,sizeof telemetry));assert(uart.enqueue(ack,sizeof ack));assert(uart.enqueue(command,sizeof command));
  assert(!uart.enqueue(command,sizeof command));
  std::vector<uint8_t> output;
  for(int i=0;i<300;++i)uart.drain(3,[&](const uint8_t *s,size_t n){output.insert(output.end(),s,s+n);return n;});
  HudUartDecoder decoder;uint8_t decoded[HUD_FRAME_MAX];std::vector<unsigned> kinds;
  for(auto byte:output)if(decoder.feed(byte,decoded,sizeof decoded))kinds.push_back(decoded[4]);
  assert((kinds==std::vector<unsigned>{10,9,8}));
  std::cout<<"PASS: live bounded parser, boot/selection guards, control fingerprint, state capacity, UART ACK priority\n";
}
