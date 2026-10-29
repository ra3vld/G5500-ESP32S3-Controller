#ifndef PIN_MAP_DESCRIBE_ONLY
#include "motion_fixture.hpp"
#include "hardware.hpp"
#include "storage.hpp"
#include "hardware_form.hpp"
#endif
#include "pin_json.hpp"
#include <cassert>
#include <iostream>
using namespace forkrot;
namespace forkrot::crowpanel { bool beginBus() {return true;} }
int main(int argc,char **argv) {
  if(SELECTED_BOARD==BOARD_S3) {
    HardwareConfig c; c.simulation=true; c.pins.led=46;
    assert(board::output(46) && board::pullup(46) && !board::validateHardware(c));
  }
  if(argc==2 && std::string(argv[1])=="--describe") {
    char data[12288]; JsonWriter j(data,sizeof data);
    j << "{\"metadata\":"; writePinCapabilities(j,false);
    j << ",\"pins\":"; writePinMap(j,PinMap{});
    j << ",\"classicEsp32\":" << unsigned(SELECTED_BOARD==BOARD_ESP32)
      << ",\"crowpanel\":" << unsigned(SELECTED_BOARD==BOARD_CROWPANEL_21)
      << ",\"buttonGpio\":" << USER_BUTTON_GPIO << '}';
    assert(j.good()); std::cout << j.data() << '\n'; return 0;
  }
  #ifndef PIN_MAP_DESCRIBE_ONLY
  if(SELECTED_BOARD==BOARD_S3) assert(board::output(46) && board::pullup(46));
  if(SELECTED_BOARD==BOARD_S2_LEGACY) assert(!board::output(46) && !board::pullup(46));
  HardwareConfig c; c.bluetooth=false; c.simulation=true;
  for(const auto &d:pinDefinitions) if(d.group==PinGroup::Drive) c.pins.*d.member=-1;
  assert(!board::validateHardware(c));
  MotionTestRuntime sim; sim.controller.configureHardware(c); Hardware h(sim); fakeHardware={};
  h.beginDirections(); h.beginAdc(); h.applyOutputs();
  int raw=1,mv=1; h.adcDiagRead(4,raw,mv);
  assert(raw==0 && mv==0 && fakeHardware.readsRaw==0 && fakeHardware.readsMv==0);
  for(const auto &e:fakeHardware.events) assert(e.operation!="attach" && e.operation!="pwm");
  c.pins.azAdc=21;
  assert(!board::validateHardware(c)); // Physical ADC wiring is inactive in simulation.
  c.pins.azAdc=-1;
  if(SELECTED_BOARD!=BOARD_ESP32) {
    c.pins.azNegative=19; assert(!board::validateHardware(c)); c.pins.azNegative=-1;
  }
  c.simulation=false;
  assert(std::string(board::validateHardware(c))=="missing_pin");
  c.simulation=true; c.pins.uartRx=-1;
  if(c.pins.uartTx>=0) assert(std::string(board::validateHardware(c))=="incomplete_uart");
  c.pins.uartTx=-1; assert(!board::validateHardware(c));
  c.pins.uartDe=0;
  assert(c.rs485() && std::string(board::validateHardware(c))=="incomplete_uart");
  c.pins.uartDe=-1;
  assert(board::validateHardware(c,true));
  c.buttonGpio=-1; c.pins.led=0; c.pins.rgb=0;
  const auto conflict=board::validateHardwareDetailed(c);
  assert(conflict && conflict.pin==0 && std::string(conflict.owner)=="pin.rgb");
  c.pins.led=-1; assert(!board::validateHardware(c));
  c.buttonGpio=0; assert(board::validateHardwareDetailed(c).pin==0);
  c.buttonGpio=-1;
  if(SELECTED_BOARD==BOARD_CROWPANEL_21) {
    c.pins.led=1;
    const auto error=board::validateHardwareDetailed(c);
    assert(error && std::string(error.owner)=="crowpanel.display");
    if(!CROWPANEL_LOCAL_DRIVE) assert(std::string(board::fixedPinOwner(6))=="crowpanel.backlight");
    else assert(!board::fixedPinOwner(6));
  } else {
    c={}; c.bluetooth=false; c.simulation=false;
    std::swap(c.pins.azAdc,c.pins.elAdc);
    std::swap(c.pins.azNegative,c.pins.azPositive);
    assert(!board::validateHardware(c));
    MotionTestRuntime r; r.controller.configureHardware(c); Hardware motor(r); fakeHardware={};
    motor.beginDirections(); motor.beginAdc();
    fakeHardware.millivolts[c.pins.azAdc]=1550;
    fakeHardware.millivolts[board::PIN_AZ_ADC]=700;
    assert(motor.filteredMv(0)==1550);
    r.controller.axes[0].direction=1;
    motor.applyOutputs();
    assert(fakeHardware.levels[c.pins.azPositive]==HIGH);
    assert(fakeHardware.levels[c.pins.azNegative]==LOW);
    c.pins.azAdc=c.pins.azNegative;
    assert(std::string(board::validateHardware(c))=="invalid_adc_gpio");
    c={}; c.simulation=false; c.pins.elAdc=c.pins.elNegative=c.pins.elPositive=c.pins.elSpeedEnable=-1;
    assert(!board::validateHardware(c));
    r.controller.configureHardware(c); assert(r.controller.model==Model::G800DXA);
    Hardware az(r); fakeHardware={}; az.beginDirections(); az.applyOutputs();
    for(const auto &e:fakeHardware.events) assert(e.pin>=0);
  }
  if(SELECTED_BOARD==BOARD_S2_LEGACY) {
    HardwareConfig independent; independent.simulation=false; independent.drive=DRIVE_BTS7960;
    independent.pins.elSpeedEnable=33;
    assert(!board::validateHardware(independent));
    MotionTestRuntime r;r.controller.configureHardware(independent);Hardware motor(r);fakeHardware={};
    motor.beginDirections();motor.applyOutputs();
    assert(fakeHardware.attachCalls==4 && fakeHardware.levels[33]==LOW && fakeHardware.levels[independent.pins.azSpeedEnable]==LOW);
  }
  // A second RS-485 port requires all signals and cannot share UART1 with HC-05.
  HardwareConfig ports; ports.bluetooth=false;ports.simulation=true;
  ports.pins.hudRx=ports.pins.hc05Rx;
  assert(std::string(board::validateHardware(ports))=="incomplete_hud485");
  ports.pins.hudTx=ports.pins.hc05Tx;ports.pins.hudDe=ports.pins.hc05State;
  if(DISPLAY_KIND==DISPLAY_ST77916 && FORK_CLASSIC_ESP32) {
    // The default touch bus uses HC-05 TX/STATE; use spare pins for this fixture.
    ports.pins.hudTx=12;ports.pins.hudDe=2;
  }
  ports.buttonGpio=-1;
  if(SELECTED_BOARD==BOARD_CROWPANEL_21) ports.pins.uartRx=ports.pins.uartTx=-1;
  assert(ports.hudRs485() && !board::validateHardware(ports,true));
  ports.bluetooth=true;ports.bluetoothMode=BT_HC05;
  assert(std::string(board::validateHardware(ports))=="uart1_conflict");
  ports.bluetooth=false;ports.pins.uartRx=ports.pins.uartTx=-1;
  assert(!board::validateHardware(ports,true)); // Controller needs only its HUD link.
  ports.pins.hudTx=ports.pins.hudDe;
  assert(std::string(board::validateHardware(ports))=="gpio_conflict");
  // Both maps, CRC, legacy migration, and candidate/confirmed separation.
  HardwareRecord record; record.hasConfirmed=true; record.phase=ConfigPhase::Confirmed;
  record.good.simulation=record.candidate.simulation=true;
  record.good.pins.uartDe=0;record.candidate.pins.hudRx=4;record.candidate.pins.hudTx=5;record.candidate.pins.hudDe=6;
  record.good.pins.led=-1; record.candidate.pins.led=0; record.candidate.buttonGpio=-1;
  const auto bytes=encodeHardware(record); HardwareRecord decoded;
  assert(bytes[2]==9 && decodeHardware(bytes,decoded));
  assert(decoded.good==record.good && decoded.candidate==record.candidate);
  auto broken=bytes; broken[36]^=1; assert(!decodeHardware(broken,decoded));
  const auto v2=encodeHardwareRecord<98>(record);
  assert(v2[2]==2 && decodeHardware(v2,decoded));
  for(size_t i=0;i<29;++i) assert(decoded.good.pins.*pinDefinitions[i].member==record.good.pins.*pinDefinitions[i].member);
  record.good.local={2,1,0,100,1900,3800,200,2100,4000,180,40};
  record.good.pins.localAzAdc=SELECTED_BOARD==BOARD_ESP32?36:4;
  auto v3=encodeHardware(record);
  assert(decodeHardware(v3,decoded) && decoded.good==record.good);
  const auto oldSensors=encodeHardwareRecord<194>(record);
  assert(oldSensors[2]==5 && decodeHardware(oldSensors,decoded));
  assert(decoded.good==record.good && decoded.good.pins.hudReady==-1);
  const auto oldRadio=encodeHardwareRecord<196>(record);
  assert(oldRadio[2]==6 && decodeHardware(oldRadio,decoded) && decoded.good==record.good);
  record.good.sensors[6]=1;record.candidate.sensors[7]=1;
  const auto oldEnable=encodeHardwareRecord<208>(record);
  assert(oldEnable[2]==8 && decodeHardware(oldEnable,decoded));
  assert(decoded.good==record.good && decoded.candidate==record.candidate);
  const auto oldInverted=encodeHardwareRecord<204>(record);
  assert(oldInverted[2]==7 && decodeHardware(oldInverted,decoded));
  assert(decoded.good==record.good && decoded.candidate==record.candidate);
  assert(decoded.good.pins.azEnable==-1 && decoded.good.pins.elEnable==-1);
  record.good.sensors[6]=record.candidate.sensors[7]=0;
  auto radio=record.good; radio.pins.uartDe=-1; radio.pins.hudReady=radio.pins.uartTx;
  assert(std::string(board::validateHardware(radio))=="gpio_conflict");
  const auto oldUart=encodeHardwareRecord<162>(record);
  assert(oldUart[2]==4 && decodeHardware(oldUart,decoded));
  assert(decoded.good.sensors==SensorSettings({0,6,0,0,6,0}));
  const auto oldLocal=encodeHardwareRecord<154>(record);
  assert(oldLocal[2]==3 && decodeHardware(oldLocal,decoded));
  assert(decoded.good.local==record.good.local && decoded.good.pins.uartDe==-1);
  record.good.pins.uartDe=-1;
  const auto legacy=encodeHardwareRecord<40>(record);
  assert(legacy[2]==1 && decodeHardware(legacy,decoded));
  assert(decoded.good.pins==PinMap{} && decoded.candidate.pins==PinMap{});
  if(SELECTED_BOARD==BOARD_CROWPANEL_21) {
    auto old=record; old.good.bluetooth=old.candidate.bluetooth=true;
    old.good.bluetoothMode=old.candidate.bluetoothMode=BT_HC05;
    assert(decodeHardware(encodeHardwareRecord<40>(old),decoded));
    assert(decoded.good.pins.uartRx==-1 && decoded.good.pins.uartTx==-1);
    assert(!board::validateHardware(decoded.good));
  }
  Preferences::database.clear(); Preferences::failedBytesKey.clear();
  HardwareRecord prior;prior.hasConfirmed=true;prior.good.simulation=prior.candidate.simulation=true;
  auto priorBytes=encodeHardwareRecord<154>(prior);
  Preferences::database["hardware"]["config1"]=Preferences::Bytes(priorBytes.begin(),priorBytes.end());
  Storage previousStorage;MotionTestRuntime previous;previousStorage.loadHardware(previous);
  prior.good.releaseSimulationPins();
  assert(!previous.recovery && previous.hardwareRecord.good==prior.good);
  assert(!previous.controller.hardware.rs485() && !previous.controller.hardware.hudRs485());
  assert(std::get<Preferences::Bytes>(Preferences::database["hardware"]["config1"]).size()==HardwareBytes{}.size());
  // Load the last released format through Storage, retaining both maps and
  // the tail fields while adding defaults for the six new display pin roles.
  Preferences::database.clear();
  HardwareRecord v8Record;v8Record.hasConfirmed=true;v8Record.phase=ConfigPhase::Confirmed;
  v8Record.good.simulation=v8Record.candidate.simulation=true;
  v8Record.good.sensors[6]=1;v8Record.candidate.sensors[7]=1;
  v8Record.good.pins.led=v8Record.candidate.pins.led=-1;
  v8Record.good.local[10]=37;v8Record.candidate.local[10]=42;
  v8Record.generation=17;
  const auto v8Bytes=encodeHardwareRecord<208>(v8Record);
  Preferences::database["hardware"]["config1"]=Preferences::Bytes(v8Bytes.begin(),v8Bytes.end());
  Storage v8Storage;MotionTestRuntime v8Runtime;v8Storage.loadHardware(v8Runtime);
  v8Record.good.releaseSimulationPins();v8Record.candidate.releaseSimulationPins();
  assert(!v8Runtime.recovery && v8Runtime.hardwareRecord.generation==17);
  assert(v8Runtime.hardwareRecord.good==v8Record.good && v8Runtime.hardwareRecord.candidate==v8Record.candidate);
  const auto &upgraded=std::get<Preferences::Bytes>(Preferences::database["hardware"]["config1"]);
  assert(upgraded.size()==HardwareBytes{}.size() && upgraded[2]==9);
  Preferences::database.clear();
  Preferences::database["hardware"]["config1"]=Preferences::Bytes(legacy.begin(),legacy.end());
  Storage storage; MotionTestRuntime migrated; storage.loadHardware(migrated);
  assert(!migrated.recovery && migrated.hardwareRecord.hasConfirmed);
  assert(std::get<Preferences::Bytes>(Preferences::database["hardware"]["config1"]).size()==HardwareBytes{}.size());
  assert(storage.finishHardwareBoot(migrated));
  auto candidate=migrated.controller.hardware; candidate.buttonGpio=-1; candidate.pins.led=0;
  assert(storage.stageHardware(migrated,candidate));
  MotionTestRuntime trial; storage.loadHardware(trial);
  assert(trial.hardwareTrial && trial.controller.hardware.pins.led==0 && !trial.controller.configurationLocked);
  assert(storage.confirmHardware(trial));
  MotionTestRuntime confirmed; storage.loadHardware(confirmed);
  assert(!confirmed.recovery && confirmed.controller.hardware.pins.led==0);
  record.good.pins.uartRx=3; record.good.pins.uartTx=1;
  record.candidate.pins.azPositive=1;
  auto recovery=board::recoveryHardware(record);
  assert(recovery.pins.uartRx==-1 && recovery.pins.uartTx==-1 && recovery.pins.rgb==-1 && recovery.pins.led==-1);
  HardwareForm form;
  assert(parseHardwareForm("token=1&generation=1&pin.led=-1",form,true,candidate));
  assert(form.config.pins.led==-1 && form.config.pins.azAdc==candidate.pins.azAdc);
  assert(!parseHardwareForm("token=1&generation=1&pin.led=0&pin.led=1",form,true));
  assert(!parseHardwareForm("token=1&generation=1&pin.led=-2",form,true));
  std::cout << "PASS: board " << SELECTED_BOARD << " runtime pins, simulation, ownership, axis completeness, v1/v2 NVS and recovery\n";
  #endif
}
