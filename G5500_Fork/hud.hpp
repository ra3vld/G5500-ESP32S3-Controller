// HUD/1 control link. GPL-3.0. Portable core shared with the host simulator.
#pragma once
#include "protocol.hpp"
#include "json_writer.hpp"
#include <memory>
#include <cstring>
namespace forkrot {
enum class DeviceRole : uint8_t { Controller, Hud };
enum class HudWifi : uint8_t { Existing, Host, Client };
struct HudConfig {
  DeviceRole role = DeviceRole::Controller;
  HudWifi wifi = HudWifi::Existing;
  bool uart = false;
  uint16_t port = 4534;
  uint64_t selected = 0;
};
struct HudEndpoint {
  uint32_t address = 0; // Network byte order, or Modbus address with uart=true (zero is legacy UART).
  uint16_t port = 4534;
  bool uart = false;
  bool operator==(const HudEndpoint &o) const {
    return uart == o.uart && address == o.address && (uart || port == o.port);
  }
};
struct HudPreset {
  char label[49]{};
  Target target{};
};
struct HudMotion {
  RegulatorMode mode=RegulatorMode::Simple;
  bool ready=false,kickEnabled=false,holdEnabled=false;
};
struct HudState {
  Model model = Model::G5500;
  Mode mode = Mode::Manual;
  float maximum = 450;
  float minimum = 0;
  RotationRange elRange{0,180};
  uint8_t elProfile = EL_STANDARD;
  uint8_t azProfile = AZ_P45;
  uint32_t rangeRevision = 1;
  Axis axes[2]{};
  SpeedSettings speeds[2]{};
  HudMotion motion[2]{};
  bool speedAvailable[2]{};
  float runtimeSpeed[2]{}, speedLimit[2]{};
  HudPreset presets[4];
  Target pending{}, lastTarget{0, 0, false, false};
  bool pendingValid = false, presetActive = false, targetKnown = false, speedEnabled = false,
       driveFault = false;
  bool variable[2]{};
  float tolerance[2]{};
  int presetSlot = -1;
  uint32_t stopEpoch = 0, remaining = 0;
  uint8_t sequenceIndex = 0, sequenceCount = 0, drive = 0, board = 0;
  char name[32] = "G5500", fault[64]{};
  void capture(const Controller &, const Protocol &, uint32_t);
  void apply(Controller &, Protocol &, uint32_t) const;
};
struct HudPeer {
  uint8_t wireVersion = 0, probeVersion = 1;
  uint64_t id = 0, boot = 0;
  HudEndpoint endpoint;
  char label[49]{};
  HudState state;
  uint32_t receivedAt = 0, remoteTick = 0, lastPoll = 0, stateSerial = 0;
  uint32_t querySerial[12]{}, queryAt[12]{};
  uint32_t subscribeSerial[4]{}, subscribeAt[4]{}, subscribeSent = 0;
  uint32_t liveAt = 0, liveRevision = 0;
  bool live = false;
  bool manual = false;
  uint32_t capabilitySerial = 0, capabilityAt = 0;
  bool received = false, saved = false;
  std::unique_ptr<HudPeer> next;
  bool fresh(uint32_t now, uint32_t age = 0) const {
    return received && uint32_t(now - receivedAt) < (age ? age : live ? 1000u : 2000u);
  }
};
struct HudReply {
  Result result = Result::Unavailable;
  uint64_t peer = 0;
  uint32_t id = 0;
};
class HudTransport {
public:
  virtual ~HudTransport() = default;
  virtual bool sendHud(const HudEndpoint &, const uint8_t *, size_t) = 0;
  virtual void selectionChanged() {}
};
// Wire and UART codecs never allocate, and do not depend on C++ object layout.
inline constexpr size_t HUD_FRAME_MAX = 768;
uint32_t hudCrc(const uint8_t *, size_t);
size_t hudUartEncode(const uint8_t *, size_t, uint8_t *, size_t);
class HudUartDecoder {
  uint8_t bytes_[HUD_FRAME_MAX + 8]{};
  size_t used_ = 0;
  bool overflow_ = false;

public:
  void clear() {
    used_ = 0;
    overflow_ = false;
  }
  size_t feed(uint8_t byte, uint8_t *out, size_t capacity);
};
class Hud : public CommandSink {
public:
  explicit Hud(Controller &c, Protocol &p) : controller_(c), protocol_(p) {}
  HudConfig config;
  uint64_t id = 0, boot = 0;
  uint32_t selectionEpoch = 0, now = 0;
  uint32_t received = 0, rejected = 0, duplicates = 0, timeouts = 0, busy = 0, sent = 0,
           sendErrors = 0;
  int sendErrno = 0;
  bool settingsDirty = false, presetsDirty = false, selectionDirty = false, catalogError = false;
  void begin(uint64_t node, uint64_t session);
  void setTransport(HudTransport *transport) { transport_ = transport; }
  bool isHud() const { return config.role == DeviceRole::Hud; }
  bool available() const override;
  uint32_t requestId() const override { return lastRequest_; }
  uint32_t selectionGeneration() const override { return selectionEpoch; }
  Result lastResult() const override { return lastResult_; }
  float intendedTarget(unsigned axis, float fallback) const override;
  Result submit(const ControlCommand &) override;
  Result submitAt(const ControlCommand &, uint32_t at) override;
  bool manualAvailable() const override {
    const auto *p = active();
    return available() && p && p->manual;
  }
  HudReply result(uint32_t request) const;
  void service(uint32_t at);
  void receive(const HudEndpoint &, const uint8_t *, size_t, uint32_t at);
  bool discover(HudEndpoint endpoint);
  HudPeer *peers() { return peers_.get(); }
  const HudPeer *peers() const { return peers_.get(); }
  HudPeer *peer(uint64_t) const;
  HudPeer *active() const { return peer(config.selected); }
  HudPeer *add(uint64_t node, HudEndpoint, const char *label, bool saved = true);
  bool remove(uint64_t);
  bool select(uint64_t);
  size_t count(bool saved = true) const;
  bool writePeers(JsonWriter &, size_t offset, size_t limit, bool discovered = false) const;
  void writeStatus(JsonWriter &, bool live = false) const;
  uint32_t freshnessAge() const;
  void invalidateMirror();

private:
  Controller &controller_;
  Protocol &protocol_;
  HudTransport *transport_ = nullptr;
  std::unique_ptr<HudPeer> peers_;
  struct Job {
    ControlCommand command;
    HudEndpoint endpoint;
    uint64_t peer = 0, boot = 0;
    uint32_t id = 0, ticket = 0, started = 0, sent = 0, selection = 0;
    uint8_t attempts = 0;
    bool used = false, live = false;
    uint8_t wireVersion = 1;
    uint32_t rangeRevision = 0, manualGesture = 0;
  };
  struct History {
    HudReply reply;
    uint32_t at = 0;
  };
  struct Dedup {
    uint64_t source = 0, boot = 0;
    uint32_t seq = 0, ticket = 0;
    Result result = Result::Invalid;
    bool used = false;
    uint8_t channel = 0; // Motion, AZ speed and EL speed have independent ordering.
  } dedup_[32];
  struct Lease {
    uint64_t source = 0, boot = 0;
    uint32_t command = 0, epoch = 0, renewed = 0;
    bool active = false;
  } leases_[2];
  struct HeldJog {
    uint64_t peer = 0, boot = 0;
    uint32_t command = 0, sent = 0;
    HudEndpoint endpoint;
    bool active = false;
  };
  struct Client {
    Job jobs[4]; // Slot zero is reserved for STOP / release of owned jog.
    History history[12];
    unsigned nextHistory = 0;
    HeldJog held[2];
    Job manual;
    uint32_t manualInputAt = 0, closedGesture = 0, closedWebGesture = 0;
  };
  struct Subscription {
    HudEndpoint endpoint;
    uint64_t source = 0, boot = 0;
    uint32_t serial = 0, renewed = 0, sent = 0, revision = 0;
    bool used = false, extended = false;
    uint8_t wireVersion = 1;
    uint16_t retryDelayMs = 0;
  } subscriptions_[4];
  uint32_t liveRevision_ = 0, liveHash_ = 0, controlHash_ = 0, liveChangedAt_ = 0;
  std::unique_ptr<Client> client_;
  uint32_t serial_ = 0, lastRequest_ = 0, lastPoll_ = 0, selectedPoll_ = 0, scanAt_ = 0;
  uint64_t pollCursor_ = 0;
  Result lastResult_ = Result::Ok;
  uint32_t scanSerial_ = 0;
  bool scanning_ = false;
  HudEndpoint scanEndpoint_;
  uint8_t tx_[HUD_FRAME_MAX]{};
  uint32_t nextSerial();
  void finish(Job &, Result);
  bool sendJob(Job &);
  bool sendQuery(const HudEndpoint &, uint64_t target, bool discovery = false);
  bool sendState(const HudEndpoint &, uint64_t target, uint32_t seq, bool ack = false, Result r = Result::Ok,
                 uint64_t targetBoot = 0, bool compact = false, uint8_t version = 1);
  bool subscribe(HudPeer &);
  bool sendManual(Job &);
  void releaseManual();
  void publishLive();
  void sendRenew(unsigned axis);
  Result execute(const ControlCommand &, uint64_t source, uint64_t session, uint32_t seq);
};
} // namespace forkrot
