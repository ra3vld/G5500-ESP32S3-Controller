// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include "ota_policy.hpp"
#include "build_config.hpp"
namespace forkrot {
#if OTA_SUPPORT
class Ota {
public:
  using Stop = void (*)(void *);
  void begin(Stop stop, void *context);
  void service();
  bool locked() const { return locked_; }
  bool readyToRestart() const;
  bool authorized(std::string_view token) const { return token == token_; }
  const char *start(uint32_t size);
  const char *write(uint32_t offset, std::string_view data);
  const char *finish();
  void cancel();
  const char *armArduino();
  void reboot();
  size_t status(char *data, size_t capacity) const;
private:
  void lock();
  void fail(const char *error);
  OtaCapacity capacity_;
  Stop stop_ = nullptr;
  void *context_ = nullptr;
  bool initialized_ = false, locked_ = false, writing_ = false, complete_ = false,
       arduino_ = false, restarting_ = false;
  uint32_t total_ = 0, received_ = 0, touched_ = 0, armedAt_ = 0, restartAt_ = 0;
  const char *error_ = "";
  char token_[33]{}, password_[17]{}, hostname_[32]{};
};
#else
class Ota {
  uint32_t restartAt_=0;bool restarting_=false;
public:
  using Stop=void(*)(void *);
  void begin(Stop,void *){}
  void service(){}
  bool locked() const{return false;}
  bool readyToRestart() const;
  bool authorized(std::string_view) const{return false;}
  const char *start(uint32_t){return "feature_disabled";}
  const char *write(uint32_t,std::string_view){return "feature_disabled";}
  const char *finish(){return "feature_disabled";}
  void cancel(){}
  const char *armArduino(){return "feature_disabled";}
  void reboot();
  size_t status(char *,size_t) const;
};
#endif
} // namespace forkrot
