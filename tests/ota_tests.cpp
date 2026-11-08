#include "ota.hpp"
#include <esp_ota_ops.h>
#include <esp_app_format.h>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
using namespace forkrot;
static std::string status(const Ota &ota) { char data[1536]; const auto n=ota.status(data,sizeof data); assert(n); return {data,n}; }
static void reset() {
  ESP={}; Update={}; ArduinoOTA={}; otaNow=0; otaPartitionCount=2;
  otaHasMetadata=true; otaNextIsRunning=false;
  otaPartitions[0]={0x10000,0x1e0000,16}; otaPartitions[1]={0x1f0000,0x1e0000,17};
}
int main() {
  assert(otaPercent(0,0)==0 && otaPercent(19,100)==19 && otaPercent(100,100)==99);
  assert(otaPercent(UINT32_MAX,UINT32_MAX)==99 && otaPercent(1,1,true)==100);
  {
    reset();std::vector<OtaProgress> progress;Ota observed;
    observed.begin(nullptr,&progress);
    observed.setProgress([](void *context,const OtaProgress &p) {
      static_cast<std::vector<OtaProgress> *>(context)->push_back(p);
    });
    Update.beforeBegin=[&]{assert(observed.locked()&&progress.back().stage==OtaStage::Preparing);};
    Update.beforeWrite=[&]{assert(progress.back().stage==OtaStage::Writing);};
    Update.beforeEnd=[&]{assert(progress.back().stage==OtaStage::Verifying&&progress.back().percent==99);};
    assert(!observed.start(4096));
    std::string data(2048,0);data[0]=char(ESP_IMAGE_HEADER_MAGIC);
    assert(!observed.write(0,data)&&progress.back().percent==50);
    const auto events=progress.size();assert(observed.write(0,data)&&progress.size()==events);
    assert(!observed.write(2048,data)&&progress.back().percent==99);
    assert(!observed.finish()&&progress.back().stage==OtaStage::Complete&&progress.back().percent==100);
    assert(progress.size()==6);
  }
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
    reset();std::vector<OtaProgress> failedProgress;Ota ota;ota.begin(nullptr,&failedProgress);
    ota.setProgress([](void *p,const OtaProgress &event){static_cast<std::vector<OtaProgress> *>(p)->push_back(event);});
    assert(!ota.start(4096));
    if(mode==0) { bytes[0]=0; assert(ota.write(0,bytes)); bytes[0]=char(ESP_IMAGE_HEADER_MAGIC); }
    if(mode==1) { bytes[12]=9; assert(ota.write(0,bytes)); bytes[12]=0; }
    if(mode==2) { otaNow=30000; ota.service(); }
    if(mode==3) ota.cancel();
    if(mode==4) { Update.writeOk=false; assert(ota.write(0,bytes)); }
    assert(!Update.running && ota.locked() && !ota.readyToRestart()); assert(ota.finish());
    assert(status(ota).find("\"complete\":false")!=std::string::npos);
    assert(failedProgress.back().stage==OtaStage::Error&&failedProgress.back().percent<100);
  }
  reset(); Ota corrupt; corrupt.begin(nullptr,nullptr); assert(!corrupt.start(4096));
  assert(!corrupt.write(0,bytes)); assert(!corrupt.write(2048,bytes)); Update.endOk=false;
  assert(corrupt.finish() && corrupt.locked() && !corrupt.readyToRestart());
  // ArduinoOTA is explicitly armed and expires. Authentication errors don't stop motors.
#if ARDUINO_OTA_SUPPORT
  {
    reset();std::vector<OtaProgress> events;Ota nativeUi;nativeUi.begin(nullptr,&events);
    nativeUi.setProgress([](void *p,const OtaProgress &e){static_cast<std::vector<OtaProgress> *>(p)->push_back(e);});
    assert(!nativeUi.armArduino());ArduinoOTA.start();assert(events.back().stage==OtaStage::Preparing);
    ArduinoOTA.progress(19,100);assert(events.back().stage==OtaStage::Writing&&events.back().percent==19);
    const auto count=events.size();ArduinoOTA.progress(191,1000);assert(events.size()==count);
    ArduinoOTA.progress(100,100);assert(events.back().stage==OtaStage::Verifying&&events.back().percent==99);
    ArduinoOTA.completed();assert(events.back().stage==OtaStage::Complete&&events.back().percent==100);
  }
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
