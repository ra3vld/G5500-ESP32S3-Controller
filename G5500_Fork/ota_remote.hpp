// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include "ota.hpp"
#include "ota_source.hpp"
#if OTA_SUPPORT
#include <atomic>
namespace forkrot {
// Network worker never touches Update, NVS, UI or controller state. A single
// acknowledged block crosses to the control owner for each flash write.
class OtaRemote {
public:
  explicit OtaRemote(Ota &owner):owner_(owner){}
  void begin();
  void service(bool idle);
  const char *save(std::string_view url,bool automatic);
  const char *check();
  const char *install();
  size_t status(char *,size_t) const;
  bool busy() const {return job_!=nullptr;}
  void cancel(){if(job_)job_->cancel.store(true);}
private:
  enum class Phase {Working,Manifest,Block,Finished,Failed};
  struct Job {
    std::atomic<Phase> phase{Phase::Working};
    std::atomic<bool> cancel{false};
    bool download=false;
    char url[OtaUrlLimit+1]{},version[48]{},hash[65]{},profile[80]{};
    uint32_t size=0;
    char data[OTA_CHUNK_SIZE+1]{};
    size_t count=0;
    const char *error="";
  };
  static void worker(void *);
  const char *launch(bool download);
  Ota &owner_;
  OtaSource source_{};
  Job *job_=nullptr;
  char version_[48]{},url_[OtaUrlLimit+1]{},hash_[65]{};
  uint32_t size_=0,lastCheck_=0,idleAt_=0;
  bool checked_=false,idle_=false,storageOk_=true,download_=false;
  const char *error_="";
};
}
#endif
