#pragma once
#include "Preferences.h"
#include "esp_err.h"
enum nvs_open_mode_t { NVS_READONLY, NVS_READWRITE };
struct FakeNvsHandle { Preferences prefs;std::string name; };
using nvs_handle_t=FakeNvsHandle *;
struct nvs_stats_t {
  size_t used_entries=0,free_entries=0,available_entries=0,total_entries=0,namespace_count=0;
};
inline esp_err_t fakeNvsSetError=ESP_OK, fakeNvsCommitError=ESP_OK, fakeNvsReadError=ESP_OK, fakeNvsStatsError=ESP_OK;
inline bool fakeNvsCorruptRead=false;
inline esp_err_t nvs_open(const char *name,nvs_open_mode_t mode,nvs_handle_t *handle) {
  auto h=new FakeNvsHandle;h->name=name;
  if(!h->prefs.begin(name,mode==NVS_READONLY)){h->prefs.end();delete h;return ESP_ERR_NVS_NOT_FOUND;}
  *handle=h;return ESP_OK;
}
inline void nvs_close(nvs_handle_t handle) {handle->prefs.end();delete handle;}
inline esp_err_t nvs_set_blob(nvs_handle_t handle,const char *key,const void *bytes,size_t size) {
  if(fakeNvsSetError!=ESP_OK)return fakeNvsSetError;
  return handle->prefs.putBytes(key,bytes,size)==size?ESP_OK:ESP_ERR_NVS_NOT_ENOUGH_SPACE;
}
inline esp_err_t nvs_commit(nvs_handle_t) {return fakeNvsCommitError;}
inline esp_err_t nvs_get_blob(nvs_handle_t handle,const char *key,void *bytes,size_t *size) {
  if(fakeNvsReadError!=ESP_OK)return fakeNvsReadError;
  const auto &space=Preferences::database[handle->name];const auto found=space.find(key);
  if(found==space.end()){*size=0;return ESP_ERR_NVS_NOT_FOUND;}
  const auto blob=std::get_if<Preferences::Bytes>(&found->second);
  if(!blob)return ESP_ERR_NVS_TYPE_MISMATCH;
  const auto capacity=*size;*size=blob->size();
  if(!bytes)return ESP_OK;
  if(capacity<blob->size())return ESP_ERR_NVS_INVALID_LENGTH;
  std::memcpy(bytes,blob->data(),blob->size());
  if(fakeNvsCorruptRead && !blob->empty())static_cast<uint8_t *>(bytes)[0]^=1;
  return ESP_OK;
}
inline esp_err_t nvs_get_stats(const char *,nvs_stats_t *stats) {
  *stats={};if(fakeNvsStatsError!=ESP_OK)return fakeNvsStatsError;
  stats->namespace_count=Preferences::database.size();stats->total_entries=Preferences::totalEntries;
  stats->used_entries=Preferences::usedEntries();
  stats->free_entries=stats->total_entries>stats->used_entries?stats->total_entries-stats->used_entries:0;
  stats->available_entries=Preferences::availableEntries();
  return ESP_OK;
}
