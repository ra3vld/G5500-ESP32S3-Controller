#include "ota_remote.hpp"
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_http_client.h>
#include <freertos/task.h>
#include <mbedtls/sha256.h>
#include <cassert>
#include <iostream>
using namespace forkrot;
static std::string remote(const Ota &o){char b[1536];auto n=o.remoteStatus(b,sizeof b);assert(n);return {b,n};}
static std::string status(const Ota &o){char b[1536];auto n=o.status(b,sizeof b);assert(n);return {b,n};}
static void drain(Ota &o){
  for(unsigned i=0;i<3000 && o.remoteBusy();++i){o.serviceRemote(false);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  assert(!o.remoteBusy());for(auto &t:otaWorkers)t.join();otaWorkers.clear();
}
static std::string digest(const std::string &s){mbedtls_sha256_context c;mbedtls_sha256_init(&c);mbedtls_sha256_update(&c,reinterpret_cast<const unsigned char *>(s.data()),s.size());unsigned char h[32];mbedtls_sha256_finish(&c,h);char b[65]{};for(unsigned i=0;i<32;++i)std::snprintf(b+i*2,3,"%02x",h[i]);return b;}
static const std::string source="https://updates.test/ota.json",binary="https://updates.test/app.bin";
static const std::string github="https://github.com/ra3vld/G5500-ESP32S3-Controller/releases/latest/download/ota-test.json";
static std::string image(5001,'\0');
static std::string manifest(std::string profile="test",std::string hash=""){
 return "{\"schema\":1,\"profile\":\""+profile+"\",\"version\":\"99.0.0\",\"size\":"+std::to_string(image.size())+",\"sha256\":\""+(hash.empty()?digest(image):hash)+"\",\"url\":\""+binary+"\"}";
}
static void reset(){assert(otaWorkers.empty());ESP={};Update={};ArduinoOTA={};fakeHardware.now=0;Preferences::database.clear();Preferences::failedBytesKey.clear();fakeHeapFree=200000;fakeHeapBlock=100000;otaRequests.clear();otaReplies.clear();otaTaskFailure=false;image[0]=char(0xe9);otaReplies[source]={manifest()};otaReplies[github]={manifest()};otaReplies[binary]={image};}
int main(){
 assert(otaUrl(source)&&otaUrl("http://192.168.1.2/ota.json?x=1"));
 for(auto bad:{"ftp://example/a","http://","https://user@host/a","http://host/a\n","http://host/#x"})assert(!otaUrl(bad));
 assert(otaNewer("2.0.0","1.99.0")&&otaNewer("1.2.0","1.2.0-dev.9+gab"));
 assert(!otaNewer("1.2.0","1.2.0+gab")&&!otaNewer("1.1.0","1.2.0")&&!otaNewer("01.2.0","1.0.0"));
 reset();Ota o;o.begin(nullptr,nullptr);assert(!o.saveSource(source,true));
 Ota restored;restored.begin(nullptr,nullptr);assert(remote(restored).find(source)!=std::string::npos&&remote(restored).find("\"automatic\":true")!=std::string::npos);
 Preferences::failedBytesKey="source1";assert(restored.saveSource("http://other.test/x",false));assert(remote(restored).find(source)!=std::string::npos);Preferences::failedBytesKey.clear();
 assert(!o.checkRemote());assert(o.saveSource("",false));drain(o);assert(remote(o).find("\"newer\":true")!=std::string::npos);
 assert(!o.installRemote());assert(o.locked());drain(o);assert(status(o).find("\"complete\":true")!=std::string::npos&&Update.count==image.size());
 for(const std::string mode:{"profile","hash","truncated","oversize","redirect","cancel","chip","task","memory","manifest"}){
  reset();Ota f;f.begin(nullptr,nullptr);assert(!f.saveSource(source,false));
  if(mode=="profile")otaReplies[source].body=manifest("other");
  if(mode=="hash")otaReplies[source].body=manifest("test",std::string(64,'0'));
  if(mode=="manifest")otaReplies[source].body="{}";
  if(mode=="redirect")otaReplies[source]={"",302,"http://updates.test/insecure"};
  if(mode=="oversize")otaReplies[source].body=std::string(2050,'x');
  assert(!f.checkRemote());drain(f);
  if(mode=="profile"||mode=="manifest"||mode=="redirect"||mode=="oversize"){assert(f.installRemote());assert(!f.locked());continue;}
  if(mode=="truncated")otaReplies[binary].body.resize(4097);
  if(mode=="chip")otaReplies[binary].body[12]=9;
  if(mode=="task")otaTaskFailure=true;
  if(mode=="memory")fakeHeapFree=10000;
  auto *error=f.installRemote();
  if(mode=="memory"){assert(error&&!f.locked());continue;}
  if(mode=="task"){assert(error&&f.locked());continue;}
  assert(!error);if(mode=="cancel")f.cancel();drain(f);
  assert(f.locked()&&!Update.running&&status(f).find("\"complete\":false")!=std::string::npos);
 }
 reset();Ota autoOta;autoOta.begin(nullptr,nullptr);assert(!autoOta.saveSource(source,true));
 autoOta.serviceRemote(true);fakeHardware.now=31000;autoOta.serviceRemote(false);assert(otaRequests.empty());
 autoOta.serviceRemote(true);fakeHardware.now=62000;autoOta.serviceRemote(true);drain(autoOta);assert(!autoOta.locked());
 autoOta.serviceRemote(false);fakeHardware.now=100000;autoOta.serviceRemote(true);fakeHardware.now=131000;autoOta.serviceRemote(true);assert(autoOta.locked());drain(autoOta);assert(Update.count==image.size());
 reset();Ota gh;gh.begin(nullptr,nullptr);assert(!gh.saveSource({},false));
 const auto signedUrl="https://assets.test/manifest?token="+std::string(600,'a');
 otaReplies[github]={"redirect body",302,signedUrl};otaReplies[signedUrl]={manifest()};
 assert(!gh.checkRemote());drain(gh);assert(otaRequests.front()==github && otaRequests.back()==signedUrl);
 assert(remote(gh).find("\"newer\":true")!=std::string::npos);
 reset();Ota disabled;disabled.begin(nullptr,nullptr);assert(!disabled.saveSource(source,false));
 disabled.serviceRemote(true);fakeHardware.now=31000;disabled.serviceRemote(true);drain(disabled);
 disabled.serviceRemote(true);assert(!disabled.locked() && !disabled.remoteBusy());
 fakeHardware.now=21632000;disabled.serviceRemote(true);assert(disabled.remoteBusy());drain(disabled);
 OtaSource saved;std::strcpy(saved.url,source.c_str());saved.automatic=true;
 auto record=encodeOtaSource(saved);OtaSource decoded;assert(decodeOtaSource(record,decoded));
 record[8]^=1;assert(!decodeOtaSource(record,decoded));
 Preferences p;p.begin("ota",false);p.putBytes("source1",record.data(),record.size());p.end();
 Ota corruptSource;corruptSource.begin(nullptr,nullptr);assert(corruptSource.checkRemote());
 assert(remote(corruptSource).find("source_storage")!=std::string::npos);
 std::cout<<"Remote OTA: persisted source, NVS failures, releases, idle scheduler, worker transfer, hash/chip/profile checks, redirects, cancellation and memory failures passed\n";
}
