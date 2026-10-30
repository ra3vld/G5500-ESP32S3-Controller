#pragma once
#include "Arduino.h"
#include <stdexcept>
#include <variant>
#include <functional>

class Preferences {
public:
  using Bytes = std::vector<uint8_t>;
  using Value = std::variant<bool, uint8_t, uint16_t, uint32_t, int32_t, uint64_t, float, String, Bytes>;
  using Database = std::map<std::string, std::map<std::string, Value>>;
  inline static Database database;
  inline static std::vector<std::pair<std::string, bool>> opened;
  inline static bool beginSucceeds = true, readOnlyBeginSucceeds = true;
  inline static std::string failedStringKey, corruptStringKey, failedBytesKey;
  inline static std::string failedKey, failedBeginNamespace;
  inline static std::string failedRemoveKey;
  inline static size_t totalEntries = 630, reservedEntries = 126;
  inline static std::function<void(const char *)> beforeBytesWrite;
  inline static std::function<void(const char *)> beforeWrite;
  inline static std::function<void(const char *)> beforeRemove;
  static size_t entryCost(const Value &value) {
    if(const auto text=std::get_if<String>(&value))return 1+(text->length()+1+31)/32;
    if(const auto bytes=std::get_if<Bytes>(&value))return 2+(bytes->size()+31)/32;
    return 1;
  }
  static size_t usedEntries() {
    size_t used=database.size();
    for(const auto &[name,space]:database){(void)name;for(const auto &[key,value]:space){(void)key;used+=entryCost(value);}}
    return used;
  }
  static size_t availableEntries() {
    const auto occupied=usedEntries()+reservedEntries;return totalEntries>occupied?totalEntries-occupied:0;
  }

  bool begin(const char *name, bool readOnly = false) {
    if (active_) { throw std::runtime_error("nested NVS begin"); }
    if (!beginSucceeds || failedBeginNamespace==name || (readOnly && !readOnlyBeginSucceeds)) { return false; }
    name_ = name;
    active_ = true;
    readOnly_ = readOnly;
    opened.emplace_back(name_, readOnly);
    return !readOnly || database.count(name_);
  }
  void end() { active_ = false; }
  void clear() { writable(); database[name_].clear(); }
  bool remove(const char *key) {
    writable();if(beforeRemove)beforeRemove(key);
    if(failedRemoveKey==key)return false;
    return database[name_].erase(key)!=0;
  }

  uint8_t getUChar(const char *key, uint8_t fallback = 0) { return get(key, fallback); }
  uint16_t getUShort(const char *key, uint16_t fallback = 0) { return get(key, fallback); }
  uint32_t getUInt(const char *key, uint32_t fallback = 0) { return get(key, fallback); }
  int32_t getInt(const char *key, int32_t fallback = 0) { return get(key, fallback); }
  uint64_t getULong64(const char *key, uint64_t fallback = 0) { return get(key, fallback); }
  bool getBool(const char *key, bool fallback = false) { return get(key, fallback); }
  float getFloat(const char *key, float fallback = 0) { return get(key, fallback); }
  String getString(const char *key, String fallback = "") { return get(key, fallback); }
  size_t getString(const char *key, char *out, size_t capacity) {
    if(!active_)throw std::runtime_error("read from closed NVS");
    const auto space=database.find(name_);
    if(space==database.end())return 0;
    const auto found=space->second.find(key);
    if(found==space->second.end())return 0;
    const auto *value=std::get_if<String>(&found->second);
    if(!value || value->length()+1>capacity)return 0;
    std::memcpy(out,value->c_str(),value->length()+1);return value->length()+1;
  }
  bool isKey(const char *key) { return database[name_].count(key) != 0; }
  size_t getBytesLength(const char *key) { return get(key, Bytes{}).size(); }
  size_t getBytes(const char *key, void *out, size_t length) {
    const auto bytes = get(key, Bytes{});
    const auto count = std::min(length, bytes.size());
    if (count) { std::memcpy(out, bytes.data(), count); }
    return count;
  }
  size_t putUChar(const char *key, uint8_t value) { return put(key, value)?sizeof value:0; }
  size_t putUShort(const char *key, uint16_t value) { return put(key, value)?sizeof value:0; }
  size_t putUInt(const char *key, uint32_t value) { return put(key, value)?sizeof value:0; }
  size_t putInt(const char *key, int32_t value) { return put(key, value)?sizeof value:0; }
  size_t putULong64(const char *key, uint64_t value) { return put(key, value)?sizeof value:0; }
  size_t putBool(const char *key, bool value) { return put(key, value)?sizeof value:0; }
  size_t putFloat(const char *key, float value) { return put(key, value)?sizeof value:0; }
  size_t putString(const char *key, String value) {
    writable();
    if (failedStringKey == key) { return 0; }
    const auto length = value.length();
    return put(key, corruptStringKey == key ? String("verification mismatch") : std::move(value))?length:0;
  }
  size_t putBytes(const char *key, const void *value, size_t length) {
    if(beforeBytesWrite) beforeBytesWrite(key);
    if(failedBytesKey==key)return 0;
    const auto bytes = static_cast<const uint8_t *>(value);
    return put(key, Bytes(bytes, bytes + length))?length:0;
  }
private:
  template <typename T> T get(const char *key, T fallback) {
    if (!active_) { throw std::runtime_error("read from closed NVS"); }
    const auto space = database.find(name_);
    if (space == database.end()) { return fallback; }
    const auto found = space->second.find(key);
    if (found == space->second.end()) { return fallback; }
    const auto value = std::get_if<T>(&found->second);
    return value ? *value : fallback;
  }
  void writable() {
    if (!active_ || readOnly_) { throw std::runtime_error("write to closed/read-only NVS"); }
  }
  template <typename T> bool put(const char *key, T value) {
    writable();
    if(beforeWrite)beforeWrite(key);
    if(failedKey==key)return false;
    const Value next(value);
    if(availableEntries()<entryCost(next)+(database.count(name_)?0:1))return false;
    database[name_].insert_or_assign(key, Value(std::move(value)));
    return true;
  }
  std::string name_;
  bool active_ = false, readOnly_ = false;
};
