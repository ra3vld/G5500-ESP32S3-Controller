#include "ota.hpp"
#include <esp_ota_ops.h>
#include <esp_app_format.h>
#include <cassert>
#include <iostream>
#include <string>
using namespace forkrot;
static std::string status(const Ota &ota) { char data[1536]; const auto n=ota.status(data,sizeof data); assert(n); return {data,n}; }
static void reset() {
  ESP={}; Update={}; ArduinoOTA={}; otaNow=0; otaPartitionCount=2;
  otaHasMetadata=true; otaNextIsRunning=false;
  otaPartitions[0]={0x10000,0x1e0000,16}; otaPartitions[1]={0x1f0000,0x1e0000,17};
}
int main() {
  assert(httpHeader("POST /api/ota/finish HTTP/1.1\r\nX-OTA-Token:  secret\t\r\n\r\n", "x-ota-token")=="secret");
  assert(httpHeader("POST /api/ota/finish HTTP/1.1\r\nOther: value\r\n\r\n", "x-ota-token").empty());
  OtaCapacity c{4<<20,2400000,0,0,true,false};
  assert(std::string(c.reason())=="flash_too_small" && c.recommendedFlash()==8<<20);
  c.image=1500000; assert(std::string(c.reason())=="partition_layout");
  c.slots=2; c.separate=true; c.slot=1300000; assert(std::string(c.reason())=="slot_too_small");
  c.slot=1966080; assert(c.available());
  // A table with just one OTA app must not overwrite its running application.
  reset(); otaPartitionCount=1; otaNextIsRunning=true;
  Ota single; single.begin(nullptr,nullptr); assert(status(single).find("partition_layout")!=std::string::npos);
  assert(single.start(4096)); assert(!Update.running && !single.locked());
  reset(); otaHasMetadata=false; Ota noMeta; noMeta.begin(nullptr,nullptr); assert(noMeta.start(4096));
  reset(); otaPartitions[1].size=0x400000; Ota outside; outside.begin(nullptr,nullptr); assert(outside.start(4096));
  // Complete transfer, byte ordering, actual size checks and delayed reboot.
  reset(); unsigned stopped=0; Ota good; good.begin([](void *p){++*static_cast<unsigned *>(p);},&stopped);
  const auto json=status(good); const auto tokenAt=json.find("\"token\":\"")+9;
  assert(!good.authorized("") && good.authorized(json.substr(tokenAt,32)));
  assert(good.start(0) && !good.locked()); assert(good.start(2000000) && !good.locked());
  assert(!good.start(4096) && good.locked() && stopped==1);
  assert(good.start(4096));
  std::string bytes(2048,'\0'); bytes[0]=char(ESP_IMAGE_HEADER_MAGIC);
  assert(good.write(1,bytes)); assert(!good.write(0,bytes));
  assert(good.finish()); assert(!good.write(2048,bytes)); assert(!good.finish());
  assert(!good.readyToRestart()); otaNow=1500; assert(good.readyToRestart());
  assert(status(good).find("\"complete\":true")!=std::string::npos && stopped==1);
  // Invalid images and interrupted transfers leave motion locked; no success/reboot.
  for(int mode=0;mode<5;++mode) {
    reset(); Ota ota; ota.begin(nullptr,nullptr); assert(!ota.start(4096));
    if(mode==0) { bytes[0]=0; assert(ota.write(0,bytes)); bytes[0]=char(ESP_IMAGE_HEADER_MAGIC); }
    if(mode==1) { bytes[12]=9; assert(ota.write(0,bytes)); bytes[12]=0; }
    if(mode==2) { otaNow=30000; ota.service(); }
    if(mode==3) ota.cancel();
    if(mode==4) { Update.writeOk=false; assert(ota.write(0,bytes)); }
    assert(!Update.running && ota.locked() && !ota.readyToRestart()); assert(ota.finish());
    assert(status(ota).find("\"complete\":false")!=std::string::npos);
  }
  reset(); Ota corrupt; corrupt.begin(nullptr,nullptr); assert(!corrupt.start(4096));
  assert(!corrupt.write(0,bytes)); assert(!corrupt.write(2048,bytes)); Update.endOk=false;
  assert(corrupt.finish() && corrupt.locked() && !corrupt.readyToRestart());
  // ArduinoOTA is explicitly armed and expires. Authentication errors don't stop motors.
#if ARDUINO_OTA_SUPPORT
  reset(); Ota native; native.begin(nullptr,nullptr); assert(!native.armArduino() && ArduinoOTA.enabled);
  ArduinoOTA.error(OTA_AUTH_ERROR); assert(!native.locked());
  otaNow=600000; native.service(); assert(!ArduinoOTA.enabled && !native.locked());
  assert(!native.armArduino()); ArduinoOTA.start(); assert(native.locked());
  ArduinoOTA.error(OTA_CONNECT_ERROR); assert(!Update.running && !native.readyToRestart());
  reset(); Ota fs; fs.begin(nullptr,nullptr); assert(!fs.armArduino()); ArduinoOTA.command=U_SPIFFS; ArduinoOTA.start();
  assert(fs.locked() && Update.aborts && status(fs).find("firmware_only")!=std::string::npos);
#else
  reset(); Ota disabled; disabled.begin(nullptr,nullptr);
  assert(std::string(disabled.armArduino())=="feature_disabled" && !ArduinoOTA.enabled && !disabled.locked());
  disabled.service();assert(!ArduinoOTA.enabled);
#endif
  std::cout << "OTA: capacity, partitions, transfers, failures and ArduinoOTA passed\n";
}
