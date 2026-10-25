#include "storage.hpp"
#include <cassert>
#include <cstdio>
#include <ctime>
#include <string>
using namespace forkrot;

int main() {
  Preferences::database.clear();Storage storage;
  auto read=[&](){char data[STATUS_JSON_CAPACITY];JsonWriter json(data,sizeof data);assert(storage.writeResetHistoryJson(json)&&json.good());return std::string(json.data(),json.size());};
  assert(read().find("\"available\":false")!=std::string::npos);
  for(int i=0;i<23;++i)assert(storage.addResetHistoryEntry(i,String("BOOT-")+String(i)));
  auto json=read();assert(json.find("\"count\":20")!=std::string::npos);
  size_t previous=0;
  for(int i=22;i>=3;--i){const auto at=json.find("\"reason\":\"BOOT-"+std::to_string(i)+"\"");assert(at!=std::string::npos&&at>=previous);previous=at;}
  assert(json.find("\"reason\":\"BOOT-2\"")==std::string::npos);
  auto &space=Preferences::database["g5500rst"];
  assert(std::get<uint8_t>(space["head"])==3);
  space["t02"]=uint64_t(0);assert(storage.updateResetHistoryTime());
  assert(std::get<uint64_t>(space["t02"])>1700000000);
  assert(std::get<uint8_t>(space["head"])==3&&std::get<uint8_t>(space["count"])==20);
  Storage reboot;assert(!reboot.updateResetHistoryTime()); // Never relabel a previous boot.
  Preferences::failedStringKey="r03";
  assert(!storage.addResetHistoryEntry(99,"FAILED"));
  assert(std::get<uint8_t>(space["head"])==3&&std::get<uint8_t>(space["count"])==20);
  assert(!storage.updateResetHistoryTime());Preferences::failedStringKey.clear();
  Preferences::beginSucceeds=false;
  assert(!storage.addResetHistoryEntry(100,"NO NVS"));
  assert(read().find("\"available\":false")!=std::string::npos);
  Preferences::beginSucceeds=true;
  space["r02"]=String(std::string(10000,'x'));
  assert(read().find("\"reason\":\"UNKNOWN\"")!=std::string::npos);
  for(unsigned i=0;i<20;++i){char key[8];std::snprintf(key,sizeof key,"r%02u",i);space[key]=String(std::string(31,'\1'));}
  assert(read().find("\\u0001")!=std::string::npos); // Worst-case JSON escaping stays bounded.
  storage.clearResetHistory();assert(read().find("\"count\":0")!=std::string::npos);
  assert(!storage.updateResetHistoryTime());
  // Deployed string/int/time keys remain readable; no history migration erases them.
  space["head"]=uint8_t(1);space["count"]=uint8_t(1);space["t00"]=uint64_t(1700000001);space["r00"]=String("PANIC");space["c00"]=int32_t(4);
  json=read();assert(json.find("PANIC")!=std::string::npos&&json.find("1700000001")!=std::string::npos);
  std::puts("PASS: 20-boot persisted history, newest first, timestamp update without duplication, storage failures, legacy keys and bounded JSON");
}
