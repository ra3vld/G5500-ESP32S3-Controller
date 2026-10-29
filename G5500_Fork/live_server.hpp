#pragma once
#include "build_config.hpp"
#include "live.hpp"
#include "live_frame.hpp"
#if defined(ARDUINO_ARCH_ESP32) && WEB_LIVE_SUPPORT
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
  static constexpr size_t ControlCapacity=256;
  static_assert(LIVE_BINARY_SIZE<=ControlCapacity,"binary state must fit the existing small frame");
  struct StateBuffer {
    std::unique_ptr<char[]> bytes;
    size_t capacity=0;
    size_t size=0;
    uint32_t revision=0;
    unsigned users=0;
    uint32_t unusedAt=0;
  };
  struct Message { char data[LIVE_INPUT_CAPACITY+1]{}; size_t size=0; };
  struct Reply { char data[192]{}; size_t size=0; };
  struct Pending { uint32_t cid=0, request=0; };
  struct SpeedRequest {
    uint64_t boot=0;
    uint32_t cid=0, selection=0, rangeRevision=0;
    float value=0;
    bool hasRangeRevision=false;
  };
  struct ClientBuffers {
    Message command, stop;
    LiveFrame<ControlCapacity> outgoing;
    StateBuffer *snapshot=nullptr;
    char pong[125]{}; size_t pongSize=0; bool pongPending=false;
    SpeedRequest speeds[2];
    Reply replies[4]; unsigned head=0, count=0;
    Pending pending[4];
    uint32_t cancelledFrom=0, cancelledThrough=0;
    uint32_t memoryWaits=0;
  };
  struct Client {
    int fd=-1; uint32_t generation=0, lastId=0, revision=0, metadataRevision=0;
    uint32_t acceptedAt=0,controlSentAt=0;
    uint32_t ackCheckedAt=0, ackStartedAt=0;
    bool closing=false, controlOnly=false, binary=false;
    bool ackWaiting=false;
    // Stable slot metadata survives disconnects; queues exist only while used.
    std::unique_ptr<ClientBuffers> buffers;
  };
  using Clients = std::array<Client,4>;
  std::unique_ptr<Clients> clients_;
  Runtime &runtime_;
  httpd_handle_t server_=nullptr;
  StaticSemaphore_t mutexStorage_;
  SemaphoreHandle_t mutex_=nullptr;
  // A slow peer pins its frame; other peers can share the newest snapshot.
  // Four pinned frames plus the latest legacy/metadata projections. Binary
  // samples fit in each client's existing small outgoing buffer.
  std::array<std::unique_ptr<StateBuffer>,6> states_;
  unsigned stateIndex_=0, metadataIndex_=0;
  size_t stateSize_=0, metadataSize_=0;
  std::array<char,LIVE_BINARY_SIZE> binary_{};
  uint32_t metadataRevision_=0, metadataHash_=0, sampledAt_=0, sampleDelay_=0;
  uint32_t binaryMetadataRevision_=0, sampleMetadataHash_=0;
  bool legacyPending_=false;
  uint32_t controlChecks_=0, stateEncodes_=0, metadataEncodes_=0;
  uint32_t revision_=0, publishedAt_=0, controlHash_=0, wakeAt_=0;
  uint32_t workRuns_=0, framesSent_=0, queueErrors_=0, sendErrors_=0, sendDeferred_=0;
  uint32_t memoryDeferred_=0, socketDeferred_=0, sendMemoryRetries_=0, ackDeferred_=0, ackTimeouts_=0;
  uint32_t handshakeTimeouts_=0, admissionRejected_=0;
  // Preserve the last failed socket write: the HTTPD return code alone loses
  // errno and whether a frame was cut short by the bounded retry deadline.
  uint32_t sendTimeouts_=0, sendRetryErrors_=0, sendElapsedMs_=0;
  size_t sendWritten_=0, sendRequested_=0;
  int sendErrno_=0, sendRetryErrno_=0;
  bool workPending_=false;
  uint32_t selection_=0;
  char boot_[17]{};
  RotationRange ingressRange_[2]{};
  bool ingressEl_=true;
  bool ingressReady_[2]{};
  float ingressSpeedLimit_[2]{};
  static esp_err_t handler(httpd_req_t *);
  static esp_err_t opened(httpd_handle_t,int);
  static int sendSocket(httpd_handle_t,int,const char *,size_t,int);
  static void closed(httpd_handle_t,int);
  static void sendWork(void *);
  static void releaseSnapshot(ClientBuffers &b, uint32_t now) {
    if(b.snapshot){if(!--b.snapshot->users)b.snapshot->unusedAt=now;b.snapshot=nullptr;}
  }
  StateBuffer *writableState(unsigned &index, size_t size, bool metadata);
  uint32_t currentRevision(const Client &c) const {
    return c.controlOnly||c.binary?revision_:stateSize_?states_[stateIndex_]->revision:0;
  }
  bool activate(Client &); // mutex held; called only after successful upgrade
  void reply(Client &,uint32_t,const char *);
  void cancelTarget(Client &,uint32_t);
  void speedCommands(uint32_t now);
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
#elif defined(ARDUINO_ARCH_ESP32)
namespace forkrot {
class Runtime;
class LiveServer {
public:
  explicit LiveServer(Runtime &) {}
  bool begin() { return true; }
  void commands(uint32_t) {}
  void publish(uint32_t) {}
  bool started() const { return false; }
  void writeDiagnostics(JsonWriter &) {}
};
}
#endif
