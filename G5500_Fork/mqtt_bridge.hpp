// Portable MQTT/HA routing. Only service() may access Controller/Hud. GPL-3.0.
#pragma once
#include "runtime.hpp"
#include <atomic>
namespace forkrot {
#if MQTT_SUPPORT
class MqttBridge {
public:
  enum class Operation : uint8_t { Target, Speed, Mode, Stop, Execute, Preset };
  struct Request {
    uint64_t node=0;
    uint32_t selection=0,session=0,at=0,serial=0;
    float value=0;
    Operation op=Operation::Stop;
    int8_t axis=-1;
    uint8_t slot=0;
    Mode mode=Mode::Manual;
    bool selected=false;
  };
  struct Callbacks {
    void *context=nullptr;
    bool (*publish)(void *,const char *,std::string_view,bool,int)=nullptr;
    bool (*idle)(void *)=nullptr;
    bool (*registryNext)(void *,uint64_t &)=nullptr;
    bool (*registryRead)(void *,uint64_t,char *,size_t)=nullptr;
    bool (*registryWrite)(void *,uint64_t,const char *)=nullptr;
    bool (*allocate)(void *,size_t)=nullptr;
    void (*beforeCommand)(void *)=nullptr;
    void (*afterCommand)(void *,bool)=nullptr;
  };
  MqttBridge(Runtime &,uint64_t gateway,Callbacks);
  // Transport callbacks only touch atomics and the bounded ingress queue.
  void connection(bool online);
  bool receive(std::string_view topic,std::string_view payload,bool retained,uint32_t at);
  void service(uint32_t at);
  uint32_t rejected() const { return rejected_.load(); }
  uint32_t dropped() const { return dropped_.load(); }
  uint32_t commands() const { return commands_; }
  uint32_t pendingRequest() const { return pendingRequest_; }
  const char *lastResult() const { return lastResult_; }
  const char *baseTopic() const { return base_; }
  size_t nodeCount() const;
  static constexpr unsigned EntityCount=20;
private:
  struct Node {
    uint64_t id=0;
    uint32_t meta=0,sentAt=0,selection=0;
    uint8_t entity=0,resetSession=0;
    bool checked=false,cleaning=false,registered=false,configured=false;
    std::unique_ptr<Node> next;
  };
  Runtime &runtime_;
  uint64_t gateway_;
  Callbacks callbacks_;
  std::unique_ptr<Node> nodes_;
  Node *stateCursor_=nullptr,*discoveryCursor_=nullptr;
  char base_[32]{},topic_[192]{},json_[1024]{},oldPrefix_[48]{};
  HudState local_;
  std::atomic_flag lock_=ATOMIC_FLAG_INIT;
  std::array<Request,4> queue_{};
  std::array<Request,2> stops_{};
  unsigned head_=0,count_=0,stopCount_=0;
  std::atomic<bool> connected_{false},rediscover_{false};
  std::atomic<uint32_t> session_{1},selection_{0},rejected_{0},dropped_{0},serial_{0};
  uint32_t observedSession_=0,lastScan_=0,lastDiscovery_=0,commands_=0,pendingRequest_=0;
  uint32_t publishNow_=0,publishFailedAt_=0;
  bool publishBlocked_=false;
  uint64_t pendingNode_=0;
  const char *lastResult_="idle";
  bool observedConnection_=false,registryDone_=!HOME_ASSISTANT_DISCOVERY_SUPPORT,announced_=false;
  Node *ensureNode(uint64_t);
  Node *nextNode(Node *) const;
  bool wanted(uint64_t) const;
  bool fresh(uint64_t,uint32_t) const;
  const HudState *state(uint64_t,uint32_t);
  const char *nodeName(uint64_t,const HudState *) const;
  bool pop(Request &);
  void execute(const Request &,uint32_t);
  bool publish(const char *,JsonWriter &,bool,int);
  bool publishState(Node &,uint32_t,bool reset=false);
  bool discovery(Node &,uint32_t);
  uint32_t metadata(const HudState *,uint64_t) const;
  void discoveryTopic(const char *,uint64_t,unsigned);
};
#endif
}
