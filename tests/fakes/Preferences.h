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
  inline static std::function<void(const char *)> beforeBytesWrite;

  bool begin(const char *name, bool readOnly = false) {
    if (active_) { throw std::runtime_error("nested NVS begin"); }
    if (!beginSucceeds || (readOnly && !readOnlyBeginSucceeds)) { return false; }
    name_ = name;
    active_ = true;
    readOnly_ = readOnly;
    opened.emplace_back(name_, readOnly);
    return !readOnly || database.count(name_);
  }
  void end() { active_ = false; }
  void clear() { writable(); database[name_].clear(); }

  uint8_t getUChar(const char *key, uint8_t fallback = 0) { return get(key, fallback); }
  uint16_t getUShort(const char *key, uint16_t fallback = 0) { return get(key, fallback); }
  uint32_t getUInt(const char *key, uint32_t fallback = 0) { return get(key, fallback); }
  int32_t getInt(const char *key, int32_t fallback = 0) { return get(key, fallback); }
  uint64_t getULong64(const char *key, uint64_t fallback = 0) { return get(key, fallback); }
  bool getBool(const char *key, bool fallback = false) { return get(key, fallback); }
  float getFloat(const char *key, float fallback = 0) { return get(key, fallback); }
  String getString(const char *key, String fallback = "") { return get(key, fallback); }
  bool isKey(const char *key) { return database[name_].count(key) != 0; }
  size_t getBytesLength(const char *key) { return get(key, Bytes{}).size(); }
  size_t getBytes(const char *key, void *out, size_t length) {
    const auto bytes = get(key, Bytes{});
    const auto count = std::min(length, bytes.size());
    if (count) { std::memcpy(out, bytes.data(), count); }
    return count;
  }
  void putUChar(const char *key, uint8_t value) { put(key, value); }
  void putUShort(const char *key, uint16_t value) { put(key, value); }
  void putUInt(const char *key, uint32_t value) { put(key, value); }
  void putInt(const char *key, int32_t value) { put(key, value); }
  void putULong64(const char *key, uint64_t value) { put(key, value); }
  void putBool(const char *key, bool value) { put(key, value); }
  void putFloat(const char *key, float value) { put(key, value); }
  size_t putString(const char *key, String value) {
    writable();
    if (failedStringKey == key) { return 0; }
    const auto length = value.length();
    put(key, corruptStringKey == key ? String("verification mismatch") : std::move(value));
    return length;
  }
  void putBytes(const char *key, const void *value, size_t length) {
    if(beforeBytesWrite) beforeBytesWrite(key);
    if(failedBytesKey==key)return;
    const auto bytes = static_cast<const uint8_t *>(value);
    put(key, Bytes(bytes, bytes + length));
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
  template <typename T> void put(const char *key, T value) {
    writable();
    database[name_].insert_or_assign(key, Value(std::move(value)));
  }
  std::string name_;
  bool active_ = false, readOnly_ = false;
};
