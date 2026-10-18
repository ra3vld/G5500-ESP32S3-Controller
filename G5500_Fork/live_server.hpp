#pragma once
#include "live.hpp"
#ifdef ARDUINO_ARCH_ESP32
// Arduino WebServer and IDF use different values for the HTTP_ANY macro.
#pragma push_macro("HTTP_ANY")
#undef HTTP_ANY
#include <esp_http_server.h>
#pragma pop_macro("HTTP_ANY")
#include <array>
#include <memory>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
namespace forkrot {
// HTTPD owns sockets; the Arduino loop alone owns Controller/Hud.
class LiveServer {
  struct Message { char data[LIVE_INPUT_CAPACITY+1]{}; size_t size=0; };
  struct Reply { char data[192]{}; size_t size=0; };
  struct Pending { uint32_t cid=0, request=0; };
  struct Client {
    int fd=-1; uint32_t generation=0, lastId=0, revision=0;
    bool closing=false;
    Message command, stop;
    Reply replies[4]; unsigned head=0, count=0;
    Pending pending[4];
    uint32_t cancelledFrom=0, cancelledThrough=0;
  };
  using Clients = std::array<Client,4>;
  std::unique_ptr<Clients> clients_;
  Runtime &runtime_;
  httpd_handle_t server_=nullptr;
  StaticSemaphore_t mutexStorage_;
  SemaphoreHandle_t mutex_=nullptr;
  std::unique_ptr<std::array<char,LIVE_STATE_CAPACITY>> state_;
  size_t stateSize_=0;
  uint32_t revision_=0, publishedAt_=0, hash_=0, controlHash_=0, wakeAt_=0;
  uint32_t workRuns_=0, framesSent_=0, queueErrors_=0, sendErrors_=0;
  bool workPending_=false;
  char boot_[17]{};
  RotationRange ingressRange_[2]{};
  bool ingressEl_=true;
  bool ingressReady_[2]{};
  static esp_err_t handler(httpd_req_t *);
  static esp_err_t opened(httpd_handle_t,int);
  static void closed(httpd_handle_t,int);
  static void sendWork(void *);
  void reply(Client &,uint32_t,const char *);
  void cancelTarget(Client &,uint32_t);
public:
  explicit LiveServer(Runtime &r):runtime_(r) {}
  bool begin();
  // Call before applying GPIO outputs, and publish after all command sources.
  void commands(uint32_t now);
  void publish(uint32_t now);
  bool started() const {return server_!=nullptr;}
  void writeDiagnostics(JsonWriter &);
};
}
#endif
