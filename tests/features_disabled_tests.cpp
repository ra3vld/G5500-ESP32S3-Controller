#include "device_service_fixture.hpp"
#include "feature_support.hpp"
int main(){
 Fixture f;
 for(const char *path:{"/api/mqtt","/api/hud/peers","/api/rssi","/api/diaglog","/api/diaglog/clear","/api/resethistory","/api/hardware/reboots","/api/ota","/api/ota/arduino","/api/hardware/display","/api/hardware/joystick/start","/api/hardware/modbus"}){
  const auto r=f.call("GET",path);if(r.status!=501)std::cerr<<path<<": "<<r.status<<" "<<r.body<<std::endl;assert(r.status==501 && r.body.find("feature_disabled")!=std::string::npos);
 }
 for(const char *body:{"n1mmPort=1234","pstManualFollow=true","gs232ManualFollow=true","rotctldManualFollow=true"}){
  const auto r=f.call("POST","/api/settings",body);assert(r.status==501&&r.body.find("feature_disabled")!=std::string::npos);
 }
 assert(f.call("POST","/api/settings","rotorName=Minimal").status==200);
 const auto status=f.call("GET","/api/status", "", false);assert(status.status==200&&status.body.find("\"mqtt\":false")!=std::string::npos);
 assert(status.body.find("\"mqtt\":{\"enabled\":false,\"storageOk\":true,\"supported\":false,\"connected\":false,\"error\":\"feature_disabled\"}")!=std::string::npos);
 // Ordinary control and protections remain in the minimal service build.
 assert(f.runtime.controller.jog(0,1,false,millis())==Result::Ok);
 assert(f.call("POST","/api/move","d=S").status==200);assert(!f.runtime.controller.moving());
 assert(f.call("POST","/api/device","role=hud&wifi=existing&uart=0&port=4534").status==501);
 const auto saved=Preferences::database;
 f.storage.clearResetHistory();f.hardware.beginInputs();f.hardware.serviceLocalInputs();
 assert(Preferences::database==saved); // Disabled services do not erase stored records.
 // A previous Bluetooth-enabled record stays latent, without forcing recovery.
 auto previous=f.runtime.hardwareRecord.good;previous.bluetooth=true;
 assert(f.storage.stageHardware(f.runtime,previous));
 Runtime restored;f.storage.loadHardware(restored);
 if(restored.recovery || !restored.hardwareTrial || !restored.controller.hardware.bluetooth)std::cerr<<"latent BT: recovery="<<restored.recovery<<" reason="<<int(restored.recoveryReason)<<" trial="<<restored.hardwareTrial<<" BT="<<restored.controller.hardware.bluetooth<<std::endl;
 assert(!restored.recovery && restored.hardwareTrial && restored.controller.hardware.bluetooth);
 assert(restored.controller.hardware.backend()==BT_NONE);
 // A saved HUD role must survive a build without HUD and keep motor GPIO idle.
 Fixture oldHud;HudConfig hudConfig;hudConfig.role=DeviceRole::Hud;
 assert(oldHud.storage.saveHudConfig(hudConfig));
 const auto roleBytes=Preferences::database["hudcfg"]["config"];
 Runtime reboot;oldHud.storage.loadHud(reboot,false);assert(reboot.hud.isHud());
 oldHud.storage.loadHardware(reboot,!HUD_SUPPORT && reboot.hud.isHud());
 reboot.setup.required=false;assert(!reboot.controller.hardware.simulation);
 fakeHardware.events.clear();Hardware motor(reboot);motor.beginDirections();motor.beginAdc();motor.applyOutputs();
 assert(fakeHardware.events.empty() && reboot.hud.isHud());
 assert(Preferences::database["hudcfg"]["config"]==roleBytes);
 std::cout<<"PASS: minimal services: explicit feature_disabled, retained configuration and working local STOP\n";
}
