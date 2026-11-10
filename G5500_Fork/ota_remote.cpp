// G5500 Fork; original by UA1CFM. GPL-3.0.
#include "ota_remote.hpp"
#if OTA_SUPPORT && G5500_HAS_OTA_PARTITIONS
#include "json_writer.hpp"
#include "project_version.hpp"
#if ARDUINO_OTA_SUPPORT
#include <ArduinoOTA.h>
#else
#include <Arduino.h>
#endif
#include <cstdio>
#include <algorithm>
#include <cmath>
#if defined(ARDUINO_ARCH_ESP32) || defined(OTA_REMOTE_TEST)
#include <Preferences.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/sha256.h>
#include <cJSON.h>
#include <new>
#endif
#ifndef G5500_OTA_PROFILE
#define G5500_OTA_PROFILE ""
#endif
namespace forkrot {
static_assert(sizeof(G5500_OTA_PROFILE)<=80,"OTA profile is too long");
void OtaRemote::begin() {
#if defined(ARDUINO_ARCH_ESP32) || defined(OTA_REMOTE_TEST)
  Preferences p;
  if(p.begin("ota",true)) {
    if(p.isKey("source1")) {
      OtaSourceBytes bytes{};
      storageOk_=p.getBytesLength("source1")==bytes.size() &&
        p.getBytes("source1",bytes.data(),bytes.size())==bytes.size() && decodeOtaSource(bytes,source_);
    }
  }
  p.end();
  if(!storageOk_)error_="source_storage";
#endif
}
const char *OtaRemote::save(std::string_view url,bool automatic) {
  if(owner_.locked() || job_)return "update_busy";
  if(url.size()>OtaUrlLimit || (!url.empty() && !otaUrl(url)))return "source_url";
#if defined(ARDUINO_ARCH_ESP32) || defined(OTA_REMOTE_TEST)
  OtaSource next;next.automatic=automatic;
  if(!url.empty())std::memcpy(next.url,url.data(),url.size());
  const auto bytes=encodeOtaSource(next);OtaSourceBytes verify{};Preferences p;
  if(!p.begin("ota",false)){p.end();return "source_storage";}
  const bool ok=p.putBytes("source1",bytes.data(),bytes.size())==bytes.size() &&
    p.getBytesLength("source1")==bytes.size() && p.getBytes("source1",verify.data(),verify.size())==verify.size() && bytes==verify;
  p.end();if(!ok)return "source_storage";
  source_=next;storageOk_=true;checked_=false;version_[0]=url_[0]=hash_[0]=0;size_=0;error_="";
  return nullptr;
#else
  (void)automatic;return "feature_disabled";
#endif
}
const char *OtaRemote::launch(bool download) {
  if(!owner_.capacity_.available())return owner_.capacity_.reason();
  if(!storageOk_)return "source_storage";
  if(owner_.locked() || job_)return "update_busy";
  if(!G5500_OTA_PROFILE[0])return "unknown_profile";
#if defined(ARDUINO_ARCH_ESP32) || defined(OTA_REMOTE_TEST)
  // Leave the controller's existing emergency reserve intact during TLS.
  if(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<65536 ||
     heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<32768)return "update_memory";
  auto *job=new(std::nothrow) Job;
  if(!job)return "update_memory";
  job->download=download;
  if(download) {
    std::strcpy(job->url,url_);std::strcpy(job->version,version_);std::strcpy(job->hash,hash_);job->size=size_;
  } else if(source_.url[0])std::strcpy(job->url,source_.url);
  else std::snprintf(job->url,sizeof job->url,
    "https://github.com/ra3vld/G5500-ESP32S3-Controller/releases/latest/download/ota-%s.json",G5500_OTA_PROFILE);
  std::strcpy(job->profile,G5500_OTA_PROFILE);
  if(download) {
    if(const auto *error=owner_.start(size_)){delete job;return error;}
  }
  job_=job;download_=download;error_="";
  if(xTaskCreate(worker,"ota-fetch",8192,job,1,nullptr)!=pdPASS){job_=nullptr;download_=false;delete job;if(download)owner_.fail("update_memory");return "update_memory";}
  if(!download){checked_=true;lastCheck_=millis();version_[0]=url_[0]=hash_[0]=0;size_=0;}
  return nullptr;
#else
  (void)download;return "feature_disabled";
#endif
}
const char *OtaRemote::check(){return launch(false);}
const char *OtaRemote::install() {
  if(!size_ || !otaNewer(version_,firmware::VERSION))return "no_new_version";
  return launch(true);
}
void OtaRemote::service(bool idle) {
  const uint32_t now=millis();
  if(!idle)idle_=false;
  else if(!idle_){idle_=true;idleAt_=now;}
  if(job_) {
    auto &job=*job_;
    if(download_ && !owner_.writing_ && !owner_.complete_)job.cancel.store(true);
    const auto phase=job.phase.load();
    if(phase==Phase::Block) {
      const char *error=nullptr;
      if(!owner_.locked())error=owner_.start(job.size);
      else if(!owner_.writing_)error="cancelled";
      if(!error)error=owner_.write(owner_.received_,{job.data,job.count});
      if(error){error_=error;job.cancel.store(true);}
      job.phase.store(Phase::Working);
    } else if(phase==Phase::Manifest || phase==Phase::Finished || phase==Phase::Failed) {
      if(phase==Phase::Manifest) {
        if(job.size>owner_.capacity_.slot)error_="image_size";
        else {size_=job.size;std::strcpy(version_,job.version);std::strcpy(url_,job.url);std::strcpy(hash_,job.hash);}
      } else if(phase==Phase::Failed) {
        if(!error_[0])error_=job.error;
        if(download_ && owner_.writing_)owner_.fail(error_);
      } else if(download_) {
        if(const auto *error=owner_.finish())error_=error;
      }
      delete job_;job_=nullptr;download_=false;
    }
    return;
  }
  if(!idle_ || uint32_t(now-idleAt_)<30000 || owner_.locked())return;
  // Check without a browser, six hours apart. Startup waits for commissioning
  // and an idle controller. Failed checks do not cause an immediate retry loop.
  if(!checked_ || uint32_t(now-lastCheck_)>=21600000) {
    checked_=true;lastCheck_=now;
    if(const auto *error=check())error_=error;
  } else if(source_.automatic && !error_[0] && size_ && otaNewer(version_,firmware::VERSION)) {
    if(const auto *error=install())error_=error;
  }
}
size_t OtaRemote::status(char *out,size_t capacity) const {
  JsonWriter j(out,capacity);
  j<<"{\"source\":"<<jsonQuoted(source_.url)<<",\"automatic\":"<<(source_.automatic?"true":"false")
   <<",\"profile\":"<<jsonQuoted(G5500_OTA_PROFILE)<<",\"busy\":"<<(job_?"true":"false")
   <<",\"downloading\":"<<(download_?"true":"false")<<",\"version\":"<<jsonQuoted(version_)
   <<",\"newer\":"<<(size_ && otaNewer(version_,firmware::VERSION)?"true":"false")
   <<",\"checked\":"<<(checked_?"true":"false")<<",\"error\":"<<jsonQuoted(error_)<<"}";
  return j.good()?j.size():0;
}
#if defined(ARDUINO_ARCH_ESP32) || defined(OTA_REMOTE_TEST)
void OtaRemote::worker(void *context) {
  auto &job=*static_cast<Job *>(context);
  mbedtls_sha256_context sha;mbedtls_sha256_init(&sha);mbedtls_sha256_starts(&sha,0);
  uint32_t received=0;
  auto deliver=[](Job &j) {
    j.phase.store(Phase::Block);
    const uint32_t start=millis();
    while(j.phase.load()==Phase::Block && !j.cancel.load()) {
      if(uint32_t(millis()-start)>=30000){j.cancel.store(true);break;}
      vTaskDelay(pdMS_TO_TICKS(1));
    }
    j.count=0;return !j.cancel.load();
  };
  esp_http_client_config_t config{};
  config.url=job.url;config.timeout_ms=10000;config.buffer_size=2048;config.buffer_size_tx=1024;
  config.crt_bundle_attach=esp_crt_bundle_attach;config.disable_auto_redirect=true;
  config.user_agent="G5500-OTA";
  auto client=esp_http_client_init(&config);
  int http=0;bool ok=false;
  if(client) {
    bool secure=std::string_view(job.url).substr(0,8)=="https://";
    for(unsigned redirects=0;redirects<=5 && !job.cancel.load();++redirects) {
      if(esp_http_client_open(client,0)!=ESP_OK || esp_http_client_fetch_headers(client)<0)break;
      http=esp_http_client_get_status_code(client);
      if(http==200){ok=true;break;}
      if(http!=301 && http!=302 && http!=303 && http!=307 && http!=308)break;
      esp_http_client_close(client);
      if(esp_http_client_set_redirection(client)!=ESP_OK)break;
      char target[2048]{};
      if(esp_http_client_get_url(client,target,sizeof target)!=ESP_OK)break;
      // GitHub's signed asset URLs can exceed the saved-source URL limit.
      const auto url=std::string_view(target);
      if((secure && url.substr(0,8)!="https://") ||
         (url.substr(0,8)!="https://" && url.substr(0,7)!="http://"))break;
      secure=secure || url.substr(0,8)=="https://";
    }
    while(ok && !job.cancel.load()) {
      const size_t room=(job.download?OTA_CHUNK_SIZE:sizeof job.data-1)-job.count;
      if(!room){ok=false;break;}
      const int n=esp_http_client_read(client,job.data+job.count,room);
      if(n<0){ok=false;break;}
      if(!n){ok=esp_http_client_is_complete_data_received(client);break;}
      if(job.download) {
        if(uint32_t(n)>job.size-received){ok=false;break;}
        mbedtls_sha256_update(&sha,reinterpret_cast<const unsigned char *>(job.data+job.count),n);
        received+=n;
      }
      job.count+=n;
      if(job.download && job.count==OTA_CHUNK_SIZE && !deliver(job)){ok=false;break;}
    }
    esp_http_client_cleanup(client);
  }
  const char *error=nullptr;
  Phase phase=Phase::Failed;
  if(job.cancel.load())error="cancelled";
  else if(!ok || http!=200)error=http==404?"source_not_found":"source_http";
  else if(job.download) {
    if(received!=job.size)error="incomplete_image";
    else {
      unsigned char digest[32];mbedtls_sha256_finish(&sha,digest);char hash[65]{};
      for(unsigned i=0;i<32;++i)std::snprintf(hash+i*2,3,"%02x",digest[i]);
      if(std::strcmp(hash,job.hash))error="image_hash";
      else if(job.count && !deliver(job))error="cancelled";
      else phase=Phase::Finished;
    }
  } else {
    job.data[job.count]=0;
    const char *end=nullptr;
    auto *json=cJSON_ParseWithLengthOpts(job.data,job.count+1,&end,true);
    auto field=[&](const char *key){return cJSON_GetObjectItemCaseSensitive(json,key);};
    auto text=[&](const char *key)->const char*{auto *v=field(key);return cJSON_IsString(v)?v->valuestring:"";};
    auto *size=field("size");auto *schema=field("schema");OtaVersion v;
    const auto version=std::string_view(text("version")),url=std::string_view(text("url")),hash=std::string_view(text("sha256"));
    bool hashOk=hash.size()==64;for(char c:hash)hashOk=hashOk && ((c>='0'&&c<='9')||(c>='a'&&c<='f'));
    if(!json || !cJSON_IsNumber(schema) || schema->valuedouble!=1 || !cJSON_IsNumber(size) ||
       !std::isfinite(size->valuedouble) || size->valuedouble<32 || size->valuedouble>UINT32_MAX || size->valuedouble!=uint32_t(size->valuedouble) ||
       !v.parse(version) || v.development || version.size()>=sizeof job.version || !otaUrl(url) || !hashOk)
      error="source_manifest";
    else if(std::strcmp(text("profile"),job.profile))error="source_profile";
    else {
      std::strcpy(job.url,text("url"));std::strcpy(job.hash,text("sha256"));std::strcpy(job.version,text("version"));
      job.size=uint32_t(size->valuedouble);phase=Phase::Manifest;
    }
    cJSON_Delete(json);
  }
  mbedtls_sha256_free(&sha);
  job.error=error?error:"";
  // Last access to job: its owner can now release it.
  job.phase.store(error?Phase::Failed:phase);
  vTaskDelete(nullptr);
}
#else
void OtaRemote::worker(void *){}
#endif
void Ota::serviceRemote(bool idle) {if(remote_)remote_->service(idle);}
const char *Ota::saveSource(std::string_view url,bool automatic){return remote_?remote_->save(url,automatic):"feature_disabled";}
const char *Ota::checkRemote(){return remote_?remote_->check():"feature_disabled";}
const char *Ota::installRemote(){return remote_?remote_->install():"feature_disabled";}
size_t Ota::remoteStatus(char *out,size_t capacity) const {return remote_?remote_->status(out,capacity):0;}
bool Ota::remoteBusy() const {return remote_ && remote_->busy();}
} // namespace forkrot
#elif OTA_SUPPORT
namespace forkrot {
void Ota::serviceRemote(bool){}
const char *Ota::saveSource(std::string_view,bool){return "feature_disabled";}
const char *Ota::checkRemote(){return "feature_disabled";}
const char *Ota::installRemote(){return "feature_disabled";}
size_t Ota::remoteStatus(char *,size_t) const{return 0;}
bool Ota::remoteBusy() const{return false;}
}
#endif
