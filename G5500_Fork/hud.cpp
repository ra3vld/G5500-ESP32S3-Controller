// HUD/1 portable transport, identity, command lifetime and mirrors. GPL-3.0.
#include "hud.hpp"
#include "network_timing.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <new>
namespace forkrot {
namespace {
constexpr uint32_t LeaseMs = 1000;
enum class Kind : uint8_t {
  Discover = 1,
  Query,
  State,
  Command,
  Ack,
  Renew,
  Subscribe,
  Telemetry,
  LiveCommand,
  LiveAck,
  ExtendedQuery,
  ExtendedState,
  ExtendedSubscribe,
  ExtendedTelemetry,
  Manual,
  ManualAck
};
struct Header {
  Kind kind{};
  uint64_t source = 0, target = 0, boot = 0, targetBoot = 0;
  uint32_t serial = 0, ticket = 0;
  uint8_t version = 1;
  uint32_t geometry = 0;
};
struct Writer {
  uint8_t *data;
  size_t capacity, size = 0;
  bool ok = true;
  void u8(uint8_t v) {
    if (size < capacity)
      data[size++] = v;
    else
      ok = false;
  }
  void u32(uint32_t v) {
    for (unsigned i = 0; i < 4; ++i)
      u8(uint8_t(v >> (i * 8)));
  }
  void u64(uint64_t v) {
    u32(uint32_t(v));
    u32(uint32_t(v >> 32));
  }
  void real(float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    u32(v);
  }
  void text(std::string_view s) {
    if (s.size() > 255) {
      ok = false;
      return;
    }
    u8(uint8_t(s.size()));
    for (auto c : s)
      u8(uint8_t(c));
  }
  size_t finish() {
    if (!ok || size + 4 > capacity)
      return 0;
    const auto crc = hudCrc(data, size);
    u32(crc);
    return ok ? size : 0;
  }
};
struct Reader {
  const uint8_t *data;
  size_t size, pos = 0;
  bool ok = true;
  uint8_t u8() {
    if (pos < size)
      return data[pos++];
    ok = false;
    return 0;
  }
  uint32_t u32() {
    uint32_t v = 0;
    for (unsigned i = 0; i < 4; ++i)
      v |= uint32_t(u8()) << (i * 8);
    return v;
  }
  uint64_t u64() {
    uint64_t lo = u32();
    return lo | (uint64_t(u32()) << 32);
  }
  float real() {
    uint32_t v = u32();
    float f;
    std::memcpy(&f, &v, 4);
    if (!std::isfinite(f))
      ok = false;
    return f;
  }
  void text(char *out, size_t capacity) {
    const auto n = u8();
    if (n >= capacity || n > size - pos) {
      ok = false;
      return;
    }
    std::memcpy(out, data + pos, n);
    out[n] = 0;
    pos += n;
    for (unsigned i = 0; i < n; ++i)
      if (uint8_t(out[i]) < 32)
        ok = false;
  }
  bool done() const { return ok && pos == size; }
};
void header(Writer &w, const Header &h) {
  for (char c : std::string_view("HUD")) w.u8(c);
  w.u8('0' + h.version);
  w.u8(uint8_t(h.kind));
  w.u64(h.source);
  w.u64(h.target);
  w.u64(h.boot);
  w.u64(h.targetBoot);
  w.u32(h.serial);
  w.u32(h.ticket);
  if (h.version >= 2) w.u32(h.geometry);
}
bool header(Reader &r, Header &h) {
  for (char c : std::string_view("HUD")) if (r.u8() != uint8_t(c)) r.ok = false;
  h.version = r.u8() - '0';
  if (h.version < 1 || h.version > 5) r.ok = false;
  h.kind = Kind(r.u8());
  h.source = r.u64();
  h.target = r.u64();
  h.boot = r.u64();
  h.targetBoot = r.u64();
  h.serial = r.u32();
  h.ticket = r.u32();
  if (h.version >= 2) h.geometry = r.u32();
  return r.ok && h.source && h.boot && uint8_t(h.kind) >= 1 && uint8_t(h.kind) <= 16;
}
void target(Writer &w, const Target &t) {
  w.real(t.az);
  w.real(t.el);
  w.u8(uint8_t(t.hasAz) | (uint8_t(t.hasEl) << 1));
}
Target target(Reader &r) {
  Target t;
  t.az = r.real();
  t.el = r.real();
  auto flags = r.u8();
  if (flags > 3)
    r.ok = false;
  t.hasAz = flags & 1;
  t.hasEl = flags & 2;
  return t;
}
void command(Writer &w, const ControlCommand &c) {
  w.u8(uint8_t(c.op));
  // Legacy 0/1 keep their meaning. Older peers reject the new follow flag.
  w.u8(uint8_t(c.external) | uint8_t(c.followInManual) << 1);
  switch (c.op) {
  case ControlOp::Target:
    target(w, c.target);
    break;
  case ControlOp::Jog:
    w.u8(uint8_t(c.axis + 1));
    w.u8(uint8_t(c.direction + 1));
    break;
  case ControlOp::Stop:
  case ControlOp::ReleaseJog:
  case ControlOp::ResetMotion:
    w.u8(uint8_t(c.axis + 1));
    break;
  case ControlOp::Mode:
    w.u8(uint8_t(c.mode));
    break;
  case ControlOp::Preset:
    w.u8(uint8_t(c.slot));
    break;
  case ControlOp::SavePreset:
    w.u8(uint8_t(c.slot));
    target(w, c.target);
    w.text(c.label);
    break;
  case ControlOp::Sequence:
    w.u8(c.count);
    w.u32(c.interval);
    for (unsigned i = 0; i < c.count && i < 16; ++i)
      target(w, c.points[i]);
    break;
  case ControlOp::Execute:
  case ControlOp::ToggleMode:
  case ControlOp::ExecuteDisplayed:
    break;
  case ControlOp::RuntimeSpeed:
    w.u8(uint8_t(c.axis)); w.real(c.speed);
    break;
  case ControlOp::ManualDrive:
    break;
  }
}
bool command(Reader &r, ControlCommand &c) {
  c.op = ControlOp(r.u8());
  auto flags = r.u8();
  c.external = flags & 1;
  c.followInManual = flags & 2;
  if (flags > 3 || (c.followInManual && c.op != ControlOp::Target && c.op != ControlOp::Sequence))
    return false;
  switch (c.op) {
  case ControlOp::Target:
    c.target = target(r);
    break;
  case ControlOp::Jog:
    c.axis = int(r.u8()) - 1;
    c.direction = int(r.u8()) - 1;
    if (c.axis < 0 || c.axis > 1 || c.direction < -1 || c.direction > 1)
      return false;
    break;
  case ControlOp::Stop:
  case ControlOp::ReleaseJog:
  case ControlOp::ResetMotion:
    c.axis = int(r.u8()) - 1;
    if (c.axis < -1 || c.axis > 1)
      return false;
    break;
  case ControlOp::RuntimeSpeed:
    c.axis = r.u8(); c.speed = r.real();
    if (c.axis > 1 || c.speed <= 0) return false;
    break;
  case ControlOp::Mode:
    c.mode = Mode(r.u8());
    if (c.mode > Mode::AutoHour)
      return false;
    break;
  case ControlOp::Preset:
    c.slot = r.u8();
    if (c.slot > 3)
      return false;
    break;
  case ControlOp::SavePreset:
    c.slot = r.u8();
    c.target = target(r);
    r.text(c.label, sizeof c.label);
    if (c.slot > 3)
      return false;
    break;
  case ControlOp::Sequence:
    c.count = r.u8();
    c.interval = r.u32();
    if (!c.count || c.count > 16)
      return false;
    for (unsigned i = 0; i < c.count; ++i)
      c.points[i] = target(r);
    break;
  case ControlOp::Execute:
  case ControlOp::ToggleMode:
  case ControlOp::ExecuteDisplayed:
    break;
  default:
    return false;
  }
  return r.done();
}
void state(Writer &w, const HudState &s, bool compact = false, bool extended = false, bool geometry = false, bool elevation = false, bool motion = false, bool speed = false) {
  w.u8(uint8_t(s.model));
  w.u8(uint8_t(s.mode));
  w.real(s.maximum);
  w.u8(s.pendingValid | s.presetActive << 1 | s.targetKnown << 2 | s.speedEnabled << 3 | s.driveFault << 4);
  w.u8(uint8_t(s.presetSlot + 1));
  w.u32(s.stopEpoch);
  w.u32(s.remaining);
  w.u8(s.sequenceIndex);
  w.u8(s.sequenceCount);
  w.u8(s.drive);
  w.u8(s.board);
  target(w, s.pending);
  target(w, s.lastTarget);
  if (!compact) w.text(s.name);
  w.text(s.fault);
  for (unsigned i = 0; i < 2; ++i) {
    const auto &a = s.axes[i];
    w.real(a.current);
    w.real(a.target);
    w.real(a.speedOutputPercent);
    w.real(s.tolerance[i]);
    w.u8(a.calibrated | a.tracking << 1 | a.completed << 2 | s.variable[i] << 3);
    w.u8(uint8_t(a.direction + 1));
    w.u8(uint8_t(a.wanted + 1));
    if (compact) continue;
    const auto &v = s.speeds[i];
    for (unsigned n : {v.speedPercent, v.startSpeedPercent, v.approachSpeedPercent, v.accelerationMs,
                       v.decelerationMs, v.slowdownDegrees})
      w.u32(n);
  }
  if (!compact) for (const auto &p : s.presets) {
    w.text(p.label);
    target(w, p.target);
  }
  if (speed) for (unsigned i = 0; i < 2; ++i) {
    w.u8(s.speedAvailable[i]); w.real(s.runtimeSpeed[i]); w.real(s.speedLimit[i]);
  }
  if (extended) {
    w.u8(s.axes[0].targetKnown | (s.axes[1].targetKnown << 1));
  }
  if (geometry) { w.real(s.minimum); w.u8(s.azProfile); w.u32(s.rangeRevision); }
  if (elevation) { w.real(s.elRange.start); w.real(s.elRange.end); w.u8(s.elProfile); }
  if(motion) for(unsigned i=0;i<2;++i) {
    const auto &m=s.motion[i]; const auto &a=s.axes[i];
    w.u8(uint8_t(m.mode));w.u8(uint8_t(m.ready)|uint8_t(m.holdEnabled)<<1|uint8_t(m.kickEnabled)<<2|uint8_t(a.holdArmed)<<3);
    w.u8(uint8_t(a.motionPhase));w.u8(uint8_t(a.motionError));
    w.real(a.measuredVelocity);w.real(a.requestedVelocity);w.real(a.profileAcceleration);
    w.real(s.speeds[i].coastDegrees);w.real(s.speeds[i].decelerationFactor);
  }
}
bool state(Reader &r, HudState &s, bool compact = false, bool extended = false, bool geometry = false, bool elevation = false, bool motion = false, bool speed = false) {
  s.model = Model(r.u8());
  s.mode = Mode(r.u8());
  s.maximum = r.real();
  auto f = r.u8();
  s.pendingValid = f & 1;
  s.presetActive = f & 2;
  s.targetKnown = f & 4;
  s.speedEnabled = f & 8;
  s.driveFault = f & 16;
  s.presetSlot = int(r.u8()) - 1;
  s.stopEpoch = r.u32();
  s.remaining = r.u32();
  s.sequenceIndex = r.u8();
  s.sequenceCount = r.u8();
  s.drive = r.u8();
  s.board = r.u8();
  s.pending = target(r);
  s.lastTarget = target(r);
  if (!compact) r.text(s.name, sizeof s.name);
  r.text(s.fault, sizeof s.fault);
  if (s.model > Model::G800DXA || s.mode > Mode::AutoHour || (!geometry && s.maximum != 360 && s.maximum != 450) ||
      s.presetSlot > 3 || s.sequenceCount > 16 || s.sequenceIndex > 16 || f > 31 || s.drive > 4)
    return false;
  for (unsigned i = 0; i < 2; ++i) {
    auto &a = s.axes[i];
    a.current = r.real();
    a.target = r.real();
    a.speedOutputPercent = r.real();
    s.tolerance[i] = r.real();
    f = r.u8();
    a.calibrated = f & 1;
    a.tracking = f & 2;
    a.completed = f & 4;
    s.variable[i] = f & 8;
    a.direction = int(r.u8()) - 1;
    a.wanted = int(r.u8()) - 1;
    if (!compact) {
    auto &v = s.speeds[i];
    v.speedPercent = r.u32();
    v.startSpeedPercent = r.u32();
    v.approachSpeedPercent = r.u32();
    v.accelerationMs = r.u32();
    v.decelerationMs = r.u32();
    v.slowdownDegrees = r.u32();
    }
    if (f > 15 || std::abs(a.direction) > 1 || std::abs(a.wanted) > 1 || a.speedOutputPercent < 0 || a.speedOutputPercent > 100)
      return false;
  }
  if (!compact) for (auto &p : s.presets) {
    char label[49]{};
    r.text(label, sizeof label);
    std::memcpy(p.label, label, sizeof label);
    p.target = target(r);
  }
  if (speed) for (unsigned i = 0; i < 2; ++i) {
    const auto available = r.u8();
    s.speedAvailable[i] = available; s.runtimeSpeed[i] = r.real(); s.speedLimit[i] = r.real();
    if (available > 1 || s.speedLimit[i] < 0 || s.runtimeSpeed[i] < 0 || s.runtimeSpeed[i] > s.speedLimit[i]) return false;
  }
  const auto known =
      extended ? r.u8()
               : uint8_t(s.targetKnown ? (s.lastTarget.hasAz | (s.lastTarget.hasEl << 1)) : 0);
  if (known > 3) {
    return false;
  }
  for (unsigned i = 0; i < 2; ++i) s.axes[i].targetKnown = known & (1u << i);
  s.minimum = geometry ? r.real() : 0;
  s.azProfile = geometry ? r.u8() : s.maximum == 360 ? AZ_P36 : AZ_P45;
  s.rangeRevision = geometry ? r.u32() : 1;
  if (elevation) { s.elRange.start=r.real(); s.elRange.end=r.real(); s.elProfile=r.u8(); }
  if(motion) for(unsigned i=0;i<2;++i) {
    auto &m=s.motion[i]; auto &a=s.axes[i]; m.mode=RegulatorMode(r.u8());const auto flags=r.u8();
    m.ready=flags&1;m.holdEnabled=flags&2;m.kickEnabled=flags&4;a.holdArmed=flags&8;
    a.motionPhase=MotionPhase(r.u8());a.motionError=MotionError(r.u8());
    a.measuredVelocity=r.real();a.requestedVelocity=r.real();a.profileAcceleration=r.real();
    s.speeds[i].coastDegrees=r.real();s.speeds[i].decelerationFactor=r.real();
    if(m.mode>RegulatorMode::Cascade || flags>15 || a.motionPhase>MotionPhase::Fault || a.motionError>MotionError::Planner ||
       s.speeds[i].coastDegrees<0 || s.speeds[i].coastDegrees>90 || s.speeds[i].decelerationFactor<.1f || s.speeds[i].decelerationFactor>10) return false;
  }
  const AzRange range{s.minimum,s.maximum};
  if (!validAzProfile(s.azProfile,range) || !s.rangeRevision ||
      !validElProfile(s.elProfile,s.elRange) || !range.contains(s.axes[0].current) || !s.elRange.contains(s.axes[1].current)) return false;
  return r.done();
}
void idText(char (&text)[17], uint64_t id) {
  std::snprintf(text, sizeof text, "%012llx", static_cast<unsigned long long>(id));
}
} // namespace
uint32_t hudCrc(const uint8_t *p, size_t n) {
  uint32_t c = ~0u;
  for (size_t i = 0; i < n; ++i) {
    c ^= p[i];
    for (unsigned b = 0; b < 8; ++b)
      c = (c >> 1) ^ (0xedb88320u & uint32_t(-int32_t(c & 1)));
  }
  return ~c;
}
size_t hudUartEncode(const uint8_t *in, size_t n, uint8_t *out, size_t cap) {
  if (!n || n > HUD_FRAME_MAX || cap < n + n / 254 + 3)
    return 0;
  size_t write = 1, codeAt = 0;
  uint8_t code = 1;
  for (size_t i = 0; i < n; ++i) {
    if (!in[i]) {
      out[codeAt] = code;
      code = 1;
      codeAt = write++;
    } else {
      out[write++] = in[i];
      if (++code == 255) {
        out[codeAt] = code;
        code = 1;
        codeAt = write++;
      }
    }
  }
  out[codeAt] = code;
  out[write++] = 0;
  return write;
}
size_t HudUartDecoder::feed(uint8_t byte, uint8_t *out, size_t cap) {
  if (byte) {
    if (used_ < sizeof bytes_)
      bytes_[used_++] = byte;
    else
      overflow_ = true;
    return 0;
  }
  const size_t n = used_;
  used_ = 0;
  if (overflow_) {
    overflow_ = false;
    return 0;
  }
  size_t in = 0, write = 0;
  while (in < n) {
    const uint8_t code = bytes_[in++];
    if (!code || in + code - 1 > n)
      return 0;
    for (unsigned i = 1; i < code; ++i) {
      if (write >= cap)
        return 0;
      out[write++] = bytes_[in++];
    }
    if (code < 255 && in < n) {
      if (write >= cap)
        return 0;
      out[write++] = 0;
    }
  }
  return write;
}
void HudState::capture(const Controller &c, const Protocol &p, uint32_t now) {
  model = c.model;
  mode = c.mode;
  maximum = c.maximum; minimum = c.minimum; azProfile = c.azProfile; rangeRevision = c.rangeRevision; elRange = c.elRange; elProfile = c.elProfile;
  pending = c.pending;
  lastTarget = c.lastTarget;
  pendingValid = c.pendingValid;
  presetActive = c.presetActive;
  targetKnown = c.targetKnown;
  presetSlot = c.presetSlot;
  speedEnabled = c.speedEnabled;
  driveFault = c.driveFault || c.setupRequired || c.configurationLocked;
  stopEpoch = c.stopEpoch;
  remaining = c.remaining(now);
  sequenceIndex = uint8_t(c.sequenceIndex());
  sequenceCount = uint8_t(c.sequenceCount());
  drive = c.hardware.simulation ? 3 : SELECTED_BOARD == BOARD_CROWPANEL_21 ? 4 : c.hardware.drive;
  board = SELECTED_BOARD;
  std::snprintf(name, sizeof name, "%s", p.rotorName.c_str());
  std::snprintf(fault, sizeof fault, "%s", c.setupRequired?"setup_incomplete":c.configurationLocked?"configuration_locked":c.fault.c_str());
  for (unsigned i = 0; i < 2; ++i) {
    axes[i] = c.axes[i];
    speeds[i] = c.speeds[i];
    speedAvailable[i] = c.runtimeSpeedAvailable(i);
    runtimeSpeed[i] = c.runtimeSpeed(i); speedLimit[i] = c.runtimeSpeedLimit(i);
    const auto &m=c.motionSettings[i];
    motion[i] = {m.mode,m.ready,m.kickEnabled,m.holdEnabled};
    variable[i] = c.variableSpeed(i);
    tolerance[i] = c.effectiveTolerance(i);
  }
  for (unsigned i = 0; i < 4; ++i) {
    std::snprintf(presets[i].label, sizeof presets[i].label, "%s", c.presets[i].label.c_str());
    presets[i].target = c.presets[i].target;
  }
}
void HudState::apply(Controller &c, Protocol &p, uint32_t now) const {
  c.model = model;
  c.mode = mode;
  c.maximum = maximum; c.minimum = minimum; c.azProfile = azProfile; c.rangeRevision = rangeRevision; c.elRange = elRange; c.elProfile = elProfile;
  c.pending = pending;
  c.lastTarget = lastTarget;
  c.pendingValid = pendingValid;
  c.presetActive = presetActive;
  c.targetKnown = targetKnown;
  c.presetSlot = presetSlot;
  c.speedEnabled = speedEnabled;
  c.driveFault = driveFault;
  c.stopEpoch = stopEpoch;
  c.fault = fault;
  c.snapshotAt = now;
  c.remoteRemaining = remaining;
  c.remoteSequenceIndex = sequenceIndex;
  c.remoteSequenceCount = sequenceCount;
  (void)p; // The HUD keeps its own stable N1MM identity across selection changes.
  for (unsigned i = 0; i < 2; ++i) {
    c.axes[i] = axes[i];
    c.speeds[i] = speeds[i];
    c.remoteSpeedAvailable[i] = speedAvailable[i];
    c.remoteSpeed[i] = runtimeSpeed[i]; c.remoteSpeedLimit[i] = speedLimit[i];
    auto &m=c.motionSettings[i]; m.mode=motion[i].mode;m.ready=motion[i].ready;
    m.kickEnabled=motion[i].kickEnabled;m.holdEnabled=motion[i].holdEnabled;
    c.remoteVariable[i] = variable[i];
    c.remoteTolerance[i] = tolerance[i];
  }
  for (unsigned i = 0; i < 4; ++i) {
    c.presets[i].label = presets[i].label;
    c.presets[i].target = presets[i].target;
  }
}
void Hud::begin(uint64_t node, uint64_t session) {
  id = node;
  boot = session ? session : 1;
  for (auto &sub : subscriptions_) sub = {};
  controller_.remote = isHud() ? this : nullptr;
  if (isHud()) {
    if (!client_)
      client_.reset(new (std::nothrow) Client);
    if (!client_)
      catalogError = true;
    invalidateMirror();
  } else
    client_.reset();
}
uint32_t Hud::nextSerial() {
  if (!++serial_)
    ++serial_;
  return serial_;
}
HudPeer *Hud::peer(uint64_t node) const {
  for (auto *p = peers_.get(); p; p = p->next.get())
    if (p->id == node)
      return p;
  return nullptr;
}
size_t Hud::count(bool saved) const {
  size_t n = 0;
  for (auto *p = peers_.get(); p; p = p->next.get())
    if (p->saved == saved)
      ++n;
  return n;
}
HudPeer *Hud::add(uint64_t node, HudEndpoint endpoint, const char *label, bool saved) {
  if (!node || node == id || !label || std::strlen(label) > 48 ||
      (!endpoint.uart && (!endpoint.address || !endpoint.port)))
    return nullptr;
  for (const unsigned char *c = reinterpret_cast<const unsigned char *>(label); *c; ++c)
    if (*c < 32)
      return nullptr;
  auto *p = peer(node);
  if (!p) {
    auto owned = std::unique_ptr<HudPeer>(new (std::nothrow) HudPeer);
    if (!owned) {
      catalogError = true;
      return nullptr;
    }
    p = owned.get();
    p->id = node;
    auto *tail = &peers_;
    while (*tail)
      tail = &(*tail)->next;
    *tail = std::move(owned);
  }
  p->endpoint = endpoint;
  p->saved = saved;
  std::snprintf(p->label, sizeof p->label, "%s", label);
  return p;
}
bool Hud::remove(uint64_t node) {
  auto *link = &peers_;
  while (*link) {
    if ((*link)->id == node) {
      if (config.selected == node)
        select(0);
      if (pollCursor_ == node)
        pollCursor_ = 0;
      auto removed = std::move(*link);
      *link = std::move(removed->next);
      return true;
    }
    link = &(*link)->next;
  }
  return false;
}
void Hud::invalidateMirror() {
  for (auto &a : controller_.axes) {
    a = Axis{};
  }
  controller_.pendingValid = false;
  controller_.targetKnown = false;
  controller_.presetActive = false;
  controller_.presetSlot = -1;
  controller_.remoteRemaining = 0;
  controller_.remoteSequenceCount = controller_.remoteSequenceIndex = 0;
  controller_.fault = "hud_offline";
  controller_.driveFault = true;
}
bool Hud::available() const {
  const auto *p = active();
  return isHud() && client_ && p && p->saved && p->fresh(now);
}
uint32_t Hud::freshnessAge() const {
  const auto *p = active();
  return p && p->received ? uint32_t(now - p->receivedAt) : UINT32_MAX;
}
bool Hud::select(uint64_t node) {
  if (!isHud() || !client_)
    return false;
  if (node && (!peer(node) || !peer(node)->saved))
    return false;
  if (node == config.selected)
    return true;
  // Cancel commands that have not been sent; sent transactions retain their destination.
  for (auto &j : client_->jobs)
    if (j.used && !j.attempts)
      finish(j, Result::Cancelled);
  bool held = false;
  for (const auto &j : client_->jobs)
    if (j.used && j.attempts && j.peer == config.selected && j.command.op == ControlOp::Jog &&
        j.command.direction)
      held = true;
  for (auto &h : client_->held)
    if (h.active && h.peer == config.selected) {
      h.active = false;
      held = true;
    }
  if (held) {
    ControlCommand c;
    c.op = ControlOp::ReleaseJog;
    submit(c);
  }
  releaseManual();
  config.selected = node;
  ++selectionEpoch;
  protocol_.resetSession();
  invalidateMirror();
  if (auto *p = active(); p && p->received) {
    p->state.apply(controller_, protocol_, p->receivedAt);
    if (!p->fresh(now))
      invalidateMirror();
  }
  if (auto *p = active()) {
    p->live = false; p->liveRevision = 0; p->subscribeSent = now - 250;
    for (auto &serial : p->subscribeSerial) {
      serial = 0;
    }
  }
  selectedPoll_ = now - 100;
  if (transport_)
    transport_->selectionChanged();
  return true;
}
float Hud::intendedTarget(unsigned axis, float fallback) const {
  if (!client_)
    return fallback;
  const Job *latest = nullptr;
  for (const auto &j : client_->jobs)
    if (j.used && j.peer == config.selected && j.selection == selectionEpoch &&
        j.command.op == ControlOp::Target && (axis ? j.command.target.hasEl : j.command.target.hasAz) &&
        (!latest || int32_t(j.id - latest->id) > 0))
      latest = &j;
  return latest ? (axis ? latest->command.target.el : latest->command.target.az) : fallback;
}
Result Hud::submitAt(const ControlCommand &c, uint32_t at) {
  // A physical sample can arrive after a slow frame, before service() advances its clock.
  now = at;
  return submit(c);
}
Result Hud::submit(const ControlCommand &c) {
  if(controller_.movementBlocked() && c.op!=ControlOp::Stop && !(c.op==ControlOp::ManualDrive && c.release))return Result::Unavailable;
  lastRequest_ = nextSerial();
  auto *p = active();
  lastResult_ = Result::Unavailable;
  if (!available() || !p)
    return lastResult_;
  if (c.op == ControlOp::RuntimeSpeed && p->wireVersion < 5) return lastResult_;
  if (c.op == ControlOp::Jog && paced(p->endpoint) && !manualAvailable()) return lastResult_;
  if(c.op==ControlOp::Target && !controller_.valid(c.target)) return lastResult_=Result::Invalid;
  if (c.op == ControlOp::ManualDrive) {
    if (!manualAvailable() || (c.absoluteSpeed && p->wireVersion < 5)) {
      return lastResult_ = Result::Unavailable;
    }
    const bool web=c.inputLeaseMs>100;
    auto &closed=web?client_->closedWebGesture:client_->closedGesture;
    if (!c.release && c.gesture == closed) {
      return lastResult_ = Result::Cancelled;
    }
    auto &j = client_->manual;
    const bool sameInput=j.command.gesture==c.gesture && (j.command.inputLeaseMs>100)==web;
    if(c.release && (!j.used || !sameInput)) {
      closed=c.gesture;return lastResult_=Result::Ok;
    }
    if (j.used && !sameInput) {
      releaseManual();
    }
    if (!j.used || !sameInput) {
      j = {};
      j.used = true;
      j.peer = p->id;
      j.boot = p->boot;
      j.endpoint = p->endpoint;
      j.selection = selectionEpoch;
      j.wireVersion = p->wireVersion; j.rangeRevision = p->state.rangeRevision;
      j.manualGesture = nextSerial();
      j.sent = now - 50;
    }
    j.command = c;
    j.id = lastRequest_;
    j.ticket = p->remoteTick;
    j.started = now;
    client_->manualInputAt = now;
    if (c.release) {
      closed = c.gesture;
      sendManual(j);
    }
    return lastResult_ = Result::Deferred;
  }
  const bool stop = c.op == ControlOp::Stop || c.op == ControlOp::ReleaseJog;
  if (c.op != ControlOp::ReleaseJog && c.op != ControlOp::RuntimeSpeed) {
    releaseManual();
  }
  // The new command must sort after the release emitted by takeover.
  lastRequest_ = nextSerial();
  Job *slot = nullptr;
  ControlCommand next=c;
  if (c.op == ControlOp::RuntimeSpeed) {
    for (auto &j : client_->jobs) if (j.used && j.command.op == ControlOp::RuntimeSpeed &&
        j.command.axis == c.axis && j.peer == p->id) {
      finish(j, Result::Cancelled); slot = &j; break;
    }
  }
  if(c.op==ControlOp::Target) {
    Job *tail=nullptr;
    for(auto &j:client_->jobs) if(j.used && (!tail || int32_t(j.id-tail->id)>0)) tail=&j;
    bool ready=true;
    if(tail) for(unsigned i=0;i<2;++i) if(i ? c.target.hasEl||tail->command.target.hasEl : c.target.hasAz||tail->command.target.hasAz)
      if((i==0 || controller_.model==Model::G5500) && (!controller_.axes[i].calibrated ||
          (p->wireVersion>=4 && controller_.variableSpeed(i) && !controller_.motionSettings[i].ready))) ready=false;
    if(ready && tail && tail->command.op==ControlOp::Target && tail->peer==p->id && tail->boot==p->boot &&
       tail->selection==selectionEpoch && tail->rangeRevision==p->state.rangeRevision &&
       tail->command.external==c.external && tail->command.followInManual==c.followInManual) {
      if(!next.target.hasAz && tail->command.target.hasAz) { next.target.hasAz=true; next.target.az=tail->command.target.az; }
      if(!next.target.hasEl && tail->command.target.hasEl) { next.target.hasEl=true; next.target.el=tail->command.target.el; }
      finish(*tail,Result::Cancelled); slot=tail;
    }
  }
  if (stop) {
    slot = &client_->jobs[0];
    if (slot->used)
      finish(*slot, Result::Cancelled);
    for (auto &j : client_->jobs)
      if (j.used && j.peer == p->id && !j.attempts)
        finish(j, Result::Cancelled);
    for (auto &h : client_->held)
      if (h.peer == p->id)
        h.active = false;
  } else if(!slot)
    for (unsigned i = 1; i < 4; ++i)
      if (!client_->jobs[i].used) {
        slot = &client_->jobs[i];
        break;
      }
  if (!slot) {
    ++busy;
    return lastResult_ = Result::Busy;
  }
  *slot = Job{};
  slot->used = true;
  slot->command = next;
  slot->peer = p->id;
  slot->boot = p->boot;
  slot->endpoint = p->endpoint;
  slot->id = lastRequest_;
  slot->ticket = p->remoteTick;
  slot->wireVersion = p->wireVersion; slot->rangeRevision = p->state.rangeRevision;
  slot->started = now;
  slot->selection = selectionEpoch;
  slot->live = p->live && uint32_t(now - p->liveAt) < 1000;
  return lastResult_ = Result::Deferred;
}
HudReply Hud::result(uint32_t request) const {
  if (!client_)
    return {Result::Unavailable, 0, request};
  for (const auto &j : client_->jobs)
    if (j.used && j.id == request)
      return {Result::Deferred, j.peer, j.id};
  for (const auto &h : client_->history)
    if (h.reply.id == request && uint32_t(now - h.at) < 15000)
      return h.reply;
  return {Result::Unavailable, 0, request};
}
void Hud::finish(Job &j, Result r) {
  client_->history[client_->nextHistory++ % 12] = {{r, j.peer, j.id}, now};
  if (r == Result::Ok && j.command.op == ControlOp::Jog && j.command.direction && j.command.axis >= 0 &&
      j.command.axis < 2 && j.peer == config.selected && j.selection == selectionEpoch) {
    auto &h = client_->held[j.command.axis];
    h = {j.peer, j.boot, j.id, now - 250, j.endpoint, true};
  }
  j.used = false;
}
bool Hud::sendManual(Job &j) {
  Writer w{tx_, sizeof tx_};
  if (!j.command.release) {
    auto *p = peer(j.peer);
    if (!p || !p->fresh(now) || p->boot != j.boot) {
      return false;
    }
    j.id = nextSerial();
    j.ticket = p->remoteTick;
  }
  header(w, {Kind::Manual, id, j.peer, boot, j.boot, j.id, j.ticket, j.wireVersion, j.rangeRevision});
  w.u32(j.manualGesture);
  w.u8(j.command.axesMask);
  w.u8(uint8_t(j.command.release) | (uint8_t(j.command.absoluteSpeed) << 1));
  w.real(j.command.manual[0]);
  w.real(j.command.manual[1]);
  const auto n = w.finish();
  j.sent = now;
  return n && transport_ && transport_->sendHud(j.endpoint, tx_, n);
}
void Hud::releaseManual() {
  if (!client_ || !client_->manual.used || client_->manual.command.release) {
    return;
  }
  auto &j = client_->manual;
  j.command.release = true;
  j.id = nextSerial();
  j.started = now;
  (j.command.inputLeaseMs>100?client_->closedWebGesture:client_->closedGesture) = j.command.gesture;
  sendManual(j);
}
bool Hud::sendQuery(const HudEndpoint &e, uint64_t targetId, bool discovery) {
  const uint32_t serial = nextSerial();
  Writer w{tx_, sizeof tx_};
  auto *selected = peer(targetId);
  const bool extended = !discovery && selected && (selected->manual || paced(e)) && selected->received && selected->fresh(now);
  const uint8_t version = selected && selected->received ? selected->wireVersion :
      selected ? (selected->probeVersion = selected->probeVersion == 1 ? 5 : selected->probeVersion - 1) : 5;
  header(w, {discovery  ? Kind::Discover
             : extended ? Kind::ExtendedQuery
                        : Kind::Query,
             id, targetId, boot, extended ? selected->boot : 0, serial, 0, version});
  const auto n = w.finish();
  if (!transport_ || !n || !transport_->sendHud(e, tx_, n))
    return false;
  if (discovery) {
    scanSerial_ = serial;
    if (!paced(e)) for (uint8_t oldVersion : {uint8_t(4),uint8_t(3),uint8_t(2),uint8_t(1)}) {
      Writer legacy{tx_,sizeof tx_}; header(legacy,{Kind::Discover,id,targetId,boot,0,serial,0,oldVersion});
      const auto bytes=legacy.finish();
      if(bytes) transport_->sendHud(e,tx_,bytes);
    }
  }
  else if (auto *p = peer(targetId)) {
    // Keep the working version first after packet loss. Probe one alternative
    // as well so a reboot into older/newer firmware can still renegotiate.
    if (p->received && !p->fresh(now) && !paced(e)) {
      p->probeVersion = p->probeVersion == 1 ? 5 : p->probeVersion - 1;
      if (p->probeVersion != version) {
        Writer probe{tx_,sizeof tx_};
        header(probe,{Kind::Query,id,targetId,boot,0,serial,0,p->probeVersion});
        const auto bytes=probe.finish();
        if(bytes)transport_->sendHud(e,tx_,bytes);
      }
    }
    for (unsigned i = 11; i > 0; --i) {
      p->querySerial[i] = p->querySerial[i - 1];
      p->queryAt[i] = p->queryAt[i - 1];
    }
    p->querySerial[0] = serial;
    p->queryAt[0] = now;
  }
  return true;
}
bool Hud::discover(HudEndpoint e) {
  scanning_ = true;
  scanAt_ = now;
  scanEndpoint_ = e;
  return sendQuery(e, 0, true);
}
bool Hud::sendState(const HudEndpoint &e, uint64_t targetId, uint32_t seq, bool ack, Result result,
                    uint64_t targetBoot, bool compact, uint8_t version) {
  if ((version == 1 && controller_.azProfile == AZ_CUSTOM) || (version < 3 && controller_.elProfile == EL_CUSTOM)) return false;
  Writer w{tx_, sizeof tx_};
  header(w, {ack ? (compact ? Kind::LiveAck : Kind::Ack) : Kind::State, id, targetId, boot, targetBoot, seq, now, version, controller_.rangeRevision});
  if (ack)
    w.u8(uint8_t(result));
  HudState s;
  s.capture(controller_, protocol_, now);
  state(w, s, compact, false, version >= 2, version >= 3, version >= 4, version >= 5);
  auto n = w.finish();
  return transport_ && n && transport_->sendHud(e, tx_, n);
}
bool Hud::sendJob(Job &j) {
  if (!j.attempts && paced(j.endpoint)) {
    if (const auto *p=peer(j.peer)) j.ticket=p->remoteTick+uint32_t(now-p->receivedAt);
  }
  Writer w{tx_, sizeof tx_};
  header(w, {j.live ? Kind::LiveCommand : Kind::Command, id, j.peer, boot, j.boot, j.id, j.ticket, j.wireVersion, j.rangeRevision});
  command(w, j.command);
  auto n = w.finish();
  if (!transport_ || !n || !transport_->sendHud(j.endpoint, tx_, n))
    return false;
  j.sent = now;
  ++j.attempts;
  return true;
}
void Hud::sendRenew(unsigned axis) {
  auto &h = client_->held[axis];
  auto *p = peer(h.peer);
  if (!p || !p->fresh(now) || p->boot != h.boot) {
    h.active = false;
    return;
  }
  Writer w{tx_, sizeof tx_};
  header(w, {Kind::Renew, id, h.peer, boot, h.boot, h.command, p->remoteTick, p->wireVersion, p->state.rangeRevision});
  w.u8(uint8_t(axis));
  auto n = w.finish();
  if (transport_ && n && transport_->sendHud(h.endpoint, tx_, n))
    h.sent = now;
}
Result Hud::execute(const ControlCommand &c, uint64_t source, uint64_t session, uint32_t seq) {
  Result r = Result::Invalid;
  switch (c.op) {
  case ControlOp::Target:
    r = controller_.target(c.target, c.external, now, c.followInManual);
    break;
  case ControlOp::Jog:
    r = controller_.jog(unsigned(c.axis), c.direction, c.external, now);
    if (r == Result::Ok && c.direction)
      leases_[c.axis] = {source, session, seq, controller_.motionEpoch[c.axis], now, true};
    break;
  case ControlOp::Stop:
    controller_.stopNow(c.axis, now);
    r = Result::Ok;
    break;
  case ControlOp::ReleaseJog:
    for (unsigned i = 0; i < 2; ++i) {
      auto &l = leases_[i];
      if ((c.axis < 0 || unsigned(c.axis) == i) && l.active && l.source == source && l.boot == session &&
          l.epoch == controller_.motionEpoch[i]) {
        controller_.stopOwnedJog(i, l.epoch, now);
        l.active = false;
      }
    }
    r = Result::Ok;
    break;
  case ControlOp::ToggleMode:
    controller_.stopNow(-1, now);
    r = controller_.setMode(controller_.mode == Mode::Manual ? Mode::Auto : Mode::Manual, now);
    settingsDirty = true;
    break;
  case ControlOp::ExecuteDisplayed:
    r = controller_.executeDisplayed(now);
    break;
  case ControlOp::ResetMotion:
    r = controller_.resetMotion(c.axis, now);
    break;
  case ControlOp::RuntimeSpeed:
    r = controller_.setRuntimeSpeed(unsigned(c.axis), c.speed, now);
    break;
  case ControlOp::ManualDrive:
    break; // Uses its own coalesced, leased stream.
  case ControlOp::Execute:
    r = controller_.execute(now);
    break;
  case ControlOp::Mode:
    r = controller_.setMode(c.mode, now);
    if (r == Result::Ok)
      settingsDirty = true;
    break;
  case ControlOp::Preset:
    r = controller_.preset(c.slot, now);
    break;
  case ControlOp::SavePreset:
    r = controller_.savePreset(c.slot, c.label, c.target);
    if (r == Result::Ok)
      presetsDirty = true;
    break;
  case ControlOp::Sequence:
    r = controller_.sequence(c.points.data(), c.count, c.interval, now, c.followInManual);
    break;
  }
  controller_.tick(now);
  return r;
}
void Hud::receive(const HudEndpoint &e, const uint8_t *data, size_t size, uint32_t at) {
  now = at;
  if (size < 49 || size > HUD_FRAME_MAX) {
    ++rejected;
    return;
  }
  Reader crc{data + size - 4, 4};
  if (hudCrc(data, size - 4) != crc.u32()) {
    ++rejected;
    return;
  }
  Reader r{data, size - 4};
  Header h;
  if (!header(r, h) || h.source == id || (h.target && h.target != id)) {
    ++rejected;
    return;
  }
  ++received;
  const bool ack = h.kind == Kind::Ack || h.kind == Kind::LiveAck;
  const bool extended = h.kind == Kind::ExtendedState || h.kind == Kind::ExtendedTelemetry;
  const bool telemetry = h.kind == Kind::Telemetry || h.kind == Kind::ExtendedTelemetry;
  const bool compact = telemetry || h.kind == Kind::LiveAck;
  if (h.kind == Kind::ManualAck) {
    const uint32_t gesture = r.u32();
    const auto result = Result(r.u8());
    if (isHud() && client_ && r.done() && h.targetBoot == boot) {
      auto &j = client_->manual;
      if (j.used && j.peer == h.source && j.boot == h.boot && j.id == h.serial &&
          j.manualGesture == gesture && j.command.release && result <= Result::Cancelled) {
        j.used = false;
      }
    }
    return;
  }
  if (h.kind == Kind::State || h.kind == Kind::ExtendedState || ack || telemetry) {
    if (!isHud() || !client_ || h.target != id || h.targetBoot != boot)
      return;
    Result result = Result::Ok;
    if (ack) {
      result = Result(r.u8());
      if (result > Result::Cancelled) {
        ++rejected;
        return;
      }
    }
    auto *p = peer(h.source);
    if (compact && (!p || !p->received || p->boot != h.boot)) return;
    const uint32_t revision = telemetry ? r.u32() : 0;
    HudState s;
    if (compact) s = p->state;
    if (!state(r, s, compact, extended, h.version >= 2, h.version >= 3, h.version >= 4, h.version >= 5) || (h.version >= 2 && h.geometry != s.rangeRevision)) {
      ++rejected;
      return;
    }
    uint32_t requestedAt = now;
    bool requested = false;
    const bool discovery =
        h.kind == Kind::State && scanning_ && h.serial == scanSerial_ && uint32_t(now - scanAt_) < responseMs(e) &&
        e.uart == scanEndpoint_.uart &&
        (e.uart || (e.port == scanEndpoint_.port &&
                    (scanEndpoint_.address == 0xffffffffu || e.address == scanEndpoint_.address)));
    if (discovery) {
      requested = true;
      requestedAt = scanAt_;
    }
    if ((h.kind == Kind::State || h.kind == Kind::ExtendedState) && p) {
      for (unsigned i = 0; i < 12; ++i) {
        if (h.serial == p->querySerial[i] && uint32_t(now - p->queryAt[i]) < responseMs(e)) {
          requested = true;
          requestedAt = p->queryAt[i];
        }
      }
    }
    if (h.kind == Kind::ExtendedState && p && h.serial == p->capabilitySerial &&
        uint32_t(now - p->capabilityAt) < responseMs(e)) {
      requested = true;
      requestedAt = p->capabilityAt;
    }
    if (ack)
      for (const auto &job : client_->jobs)
        if (job.used && job.peer == h.source && job.boot == h.boot && job.id == h.serial) {
          requested = true;
          requestedAt = job.started;
          break;
        }
    if (telemetry && p && p->id == config.selected) {
      for (unsigned i = 0; i < 4; ++i) {
        if (h.serial == p->subscribeSerial[i] && uint32_t(now - p->subscribeAt[i]) < 1000) {
          requested = true;
          requestedAt = p->subscribeAt[i];
        }
      }
      if (p->live && int32_t(revision - p->liveRevision) < 0) return;
    }
    if (!requested || uint32_t(now - requestedAt) >= responseMs(e)) {
      return; // Delayed unsolicited state cannot make an offline drive appear fresh.
    }
    if (!p) {
      if (!discovery)
        return;
      p = add(h.source, e, s.name, false);
      if (!p)
        return;
    }
    if (!(p->endpoint == e)) {
      if (discovery)
        p->endpoint = e;
      else
        return;
    }
    p->freshnessMs = paced(e)?responseMs(e)+uint32_t(config.link.intervalMs)*3:0;
    // Capability is stable for a boot even if a newer state overtook this reply.
    if (extended && (!p->boot || p->boot == h.boot)) {
      p->manual = true;
    }
    if (p->received && ((!telemetry && int32_t(h.serial - p->stateSerial) < 0) ||
                        (p->boot == h.boot && int32_t(h.ticket - p->remoteTick) < 0))) {
      if (ack)
        for (auto &j : client_->jobs)
          if (j.used && j.peer == p->id && j.boot == h.boot && j.id == h.serial) {
            finish(j, result);
            break;
          }
      return;
    }
    if (p->boot == h.boot && p->wireVersion > h.version) return;
    if (p->received && h.version >= 2 && p->boot == h.boot && p->state.rangeRevision != s.rangeRevision) {
      for (auto &j : client_->jobs) if (j.used && j.peer == p->id && j.command.op != ControlOp::Stop) finish(j,Result::Cancelled);
      for (auto &held : client_->held) if (held.peer == p->id) held.active=false;
      if (client_->manual.used && client_->manual.peer == p->id) {
        auto &manual=client_->manual;
        (manual.command.inputLeaseMs>100?client_->closedWebGesture:client_->closedGesture)=manual.command.gesture;
        manual.used=false;
      }
      if (config.selected == p->id) { ++selectionEpoch; protocol_.resetSession(); }
    }
    p->wireVersion = h.version;
    // After a dropout, try the last negotiated version before falling back.
    p->probeVersion = h.version == 5 ? 1 : h.version + 1;
    if (p->boot && p->boot != h.boot) {
      p->live = false;
      p->manual = false;
      p->liveRevision = 0;
      for (auto &serial : p->subscribeSerial) {
        serial = 0;
      }
      for (auto &j : client_->jobs)
        if (j.used && j.peer == p->id)
          finish(j, Result::Cancelled);
      for (auto &held : client_->held)
        if (held.peer == p->id)
          held.active = false;
    }
    p->boot = h.boot;
    if (!extended && p->manual && p->boot == h.boot && s.targetKnown) {
      for (unsigned i = 0; i < 2; ++i) {
        s.axes[i].targetKnown = s.axes[i].targetKnown || p->state.axes[i].targetKnown;
      }
    }
    if (extended) {
      p->manual = true;
    }
    p->state = std::move(s);
    p->received = true;
    p->receivedAt = now; // Freshness starts at a validated arrival, not its delayed request.
    p->remoteTick = h.ticket;
    if (!telemetry) p->stateSerial = h.serial;
    if (telemetry) { p->live = true; p->liveAt = now; p->liveRevision = revision; }
    if (p->saved && p->id == config.selected)
      p->state.apply(controller_, protocol_, now);
    if (ack)
      for (auto &j : client_->jobs)
        if (j.used && j.peer == p->id && j.boot == h.boot && j.id == h.serial) {
          finish(j, result);
          break;
        }
    return;
  }
  if (isHud())
    return; // No HUD-to-HUD forwarding, including accidental loops.
  if (h.kind == Kind::ExtendedQuery) {
    if ((h.version == 1 && controller_.azProfile == AZ_CUSTOM) || (h.version < 3 && controller_.elProfile == EL_CUSTOM)) return;
    if (h.target == id && h.targetBoot == boot && r.done()) {
      Writer w{tx_, sizeof tx_};
      header(w, {Kind::ExtendedState, id, h.source, boot, h.boot, h.serial, now, h.version, controller_.rangeRevision});
      HudState s;
      s.capture(controller_, protocol_, now);
      state(w, s, false, true, h.version >= 2, h.version >= 3, h.version >= 4, h.version >= 5);
      const auto n = w.finish();
      if (n && transport_) {
        transport_->sendHud(e, tx_, n);
      }
    }
    return;
  }
  if (h.kind == Kind::Discover || h.kind == Kind::Query) {
    if (r.done())
      sendState(e, h.source, h.serial, false, Result::Ok, h.boot, false, h.version);
    return;
  }
  if (h.target != id || h.targetBoot != boot || uint32_t(now - h.ticket) >= ticketMs(e)) {
    ++rejected;
    return;
  }
  const bool staleGeometry = (h.version < 3 && controller_.elProfile == EL_CUSTOM) || (h.version == 1 && controller_.azProfile == AZ_CUSTOM) ||
      (h.version >= 2 && h.geometry != controller_.rangeRevision);
  if (staleGeometry && h.kind != Kind::Command && h.kind != Kind::LiveCommand) { ++rejected; return; }
  if (h.kind == Kind::Manual) {
    const uint32_t gesture = r.u32();
    const auto mask = r.u8();
    const auto flags = r.u8();
    const bool release = flags & 1, absoluteSpeed = flags & 2;
    const float az = r.real(), el = r.real();
    if (!r.done() || flags > (h.version >= 5 ? 3 : 1)) {
      ++rejected;
      return;
    }
    Dedup *barrier = nullptr;
    bool stale = false;
    for (auto &d : dedup_) {
      if (d.used && !d.channel && d.source == h.source && d.boot == h.boot) {
        barrier = &d;
        stale = int32_t(h.serial - d.seq) <= 0;
        break;
      }
      if ((!d.used || uint32_t(now - d.ticket) >= ticketMs({0,config.port,true})) && (&d != &dedup_[0] || release)) {
        barrier = &d;
      }
    }
    const auto result = stale      ? Result::Cancelled
                        : !barrier ? Result::Busy
                                   : controller_.manualDrive(h.source, h.boot, gesture, h.serial,
                                                             mask, az, el, release, now, 100, absoluteSpeed);
    if (!stale && barrier) {
      *barrier = {h.source, h.boot, h.serial, h.ticket, result, true};
    }
    Writer w{tx_, sizeof tx_};
    header(w, {Kind::ManualAck, id, h.source, boot, h.boot, h.serial, now, h.version, controller_.rangeRevision});
    w.u32(gesture);
    w.u8(uint8_t(result));
    const auto n = w.finish();
    if (n && transport_) {
      transport_->sendHud(e, tx_, n);
    }
    return;
  }
  if (h.kind == Kind::Subscribe || h.kind == Kind::ExtendedSubscribe) {
    if (!r.done()) return;
    Subscription *slot = nullptr;
    for (auto &sub : subscriptions_) {
      if (sub.used && uint32_t(now - sub.renewed) >= 1000) sub.used = false;
      if (sub.used && sub.source == h.source && sub.boot == h.boot) {
        if (int32_t(h.serial - sub.serial) <= 0) return;
        slot = &sub; break;
      }
      if (!sub.used && !slot) slot = &sub;
    }
    if (slot && slot->used && slot->source == h.source && slot->boot == h.boot &&
        slot->endpoint == e && slot->extended == (h.kind == Kind::ExtendedSubscribe) &&
        slot->wireVersion == h.version) {
      // Renewal changes the challenge, not the pending send or its backoff.
      slot->serial = h.serial;
      slot->renewed = now;
      return;
    }
    if (slot) {
      *slot = {e,        h.source, h.boot,
               h.serial, now,      now - 250,
               0,        true,     h.kind == Kind::ExtendedSubscribe, h.version};
    }
    return;
  }
  if (h.kind == Kind::Renew) {
    const unsigned axis = r.u8();
    if (axis > 1 || !r.done())
      return;
    auto &l = leases_[axis];
    if (l.active && l.source == h.source && l.boot == h.boot && l.command == h.serial &&
        l.epoch == controller_.motionEpoch[axis] && uint32_t(now - l.renewed) < LeaseMs)
      l.renewed = now;
    return;
  }
  if (h.kind != Kind::Command && h.kind != Kind::LiveCommand)
    return;
  ControlCommand c;
  if (!command(r, c)) {
    ++rejected;
    return;
  }
  if (staleGeometry && c.op != ControlOp::Stop) {
    ++rejected; sendState(e,h.source,h.serial,true,Result::Cancelled,h.boot,h.kind==Kind::LiveCommand,h.version); return;
  }
  const uint8_t channel = c.op == ControlOp::RuntimeSpeed ? uint8_t(c.axis + 1) : 0;
  Dedup *free = nullptr;
  for (auto &d : dedup_) {
    if (d.used && uint32_t(now - d.ticket) >= ticketMs({0,config.port,true}))
      d.used = false;
    if (!d.used) {
      if (!free && (&d != &dedup_[0] || c.op == ControlOp::Stop || c.op == ControlOp::ReleaseJog))
        free = &d;
      continue;
    }
    if (d.source == h.source && d.boot == h.boot && d.channel == channel) {
      if (d.seq == h.serial) {
        ++duplicates;
        sendState(e, h.source, h.serial, true, d.result, h.boot, h.kind == Kind::LiveCommand, h.version);
        return;
      }
      if (int32_t(h.serial - d.seq) < 0) {
        sendState(e, h.source, h.serial, true, Result::Cancelled, h.boot, h.kind == Kind::LiveCommand, h.version);
        return;
      }
    }
  }
  const bool stop = c.op == ControlOp::Stop || c.op == ControlOp::ReleaseJog;
  // Reuse this source's previous entry: higher serial makes all older commands stale.
  for (auto &d : dedup_)
    if (d.used && d.source == h.source && d.boot == h.boot && d.channel == channel) {
      free = &d;
      break;
    }
  if (!free) {
    ++busy;
    sendState(e, h.source, h.serial, true, Result::Busy, h.boot, h.kind == Kind::LiveCommand, h.version);
    return;
  }
  (void)stop;
  const auto result = execute(c, h.source, h.boot, h.serial);
  *free = {h.source, h.boot, h.serial, h.ticket, result, true, channel};
  sendState(e, h.source, h.serial, true, result, h.boot, h.kind == Kind::LiveCommand, h.version);
}
bool Hud::subscribe(HudPeer &p) {
  const uint32_t seq = nextSerial();
  Writer w{tx_, sizeof tx_};
  header(w, {p.manual ? Kind::ExtendedSubscribe : Kind::Subscribe, id, p.id, boot, p.boot, seq,
             p.remoteTick, p.wireVersion, p.state.rangeRevision});
  const auto n = w.finish();
  p.subscribeSent = now;
  if (!n || !transport_ || !transport_->sendHud(p.endpoint, tx_, n)) return false;
  for (unsigned i = 3; i > 0; --i) {
    p.subscribeSerial[i] = p.subscribeSerial[i - 1];
    p.subscribeAt[i] = p.subscribeAt[i - 1];
  }
  p.subscribeSerial[0] = seq; p.subscribeAt[0] = now;
  return true;
}
void Hud::publishLive() {
  bool any = false;
  for (auto &sub : subscriptions_) {
    if (sub.used && uint32_t(now - sub.renewed) >= 1000) sub.used = false;
    any |= sub.used;
  }
  if (!any) return;
  HudState current; current.capture(controller_, protocol_, now);
  current.remaining = (current.remaining / 1000) * 1000;
  Writer fingerprint{tx_, sizeof tx_};
  state(fingerprint, current, true, true, true, true, true, true);
  const uint32_t hash = hudCrc(tx_, fingerprint.size);
  HudState control = current;
  for (auto &axis : control.axes) { axis.current = 0; axis.speedOutputPercent = 0; axis.measuredVelocity=axis.requestedVelocity=axis.profileAcceleration=0; }
  fingerprint = {tx_, sizeof tx_};
  state(fingerprint, control, true, true, true, true, true, true);
  const uint32_t controlHash = hudCrc(tx_, fingerprint.size);
  if (hash != liveHash_ && (controlHash != controlHash_ || uint32_t(now - liveChangedAt_) >= HudPositionIntervalMs)) {
    liveHash_ = hash; controlHash_ = controlHash; liveChangedAt_ = now; ++liveRevision_;
  }
  for (auto &sub : subscriptions_) {
    if ((sub.wireVersion == 1 && controller_.azProfile == AZ_CUSTOM) || (sub.wireVersion < 3 && controller_.elProfile == EL_CUSTOM)) { sub.used=false; continue; }
    if (!sub.used || (sub.revision == liveRevision_ && uint32_t(now - sub.sent) < 250)) continue;
    if (sub.retryDelayMs && uint32_t(now - sub.sent) < sub.retryDelayMs) continue;
    Writer w{tx_, sizeof tx_};
    header(w, {sub.extended ? Kind::ExtendedTelemetry : Kind::Telemetry, id, sub.source, boot,
               sub.boot, sub.serial, now, sub.wireVersion, controller_.rangeRevision});
    w.u32(liveRevision_);
    state(w, current, true, sub.extended, sub.wireVersion >= 2, sub.wireVersion >= 3, sub.wireVersion >= 4, sub.wireVersion >= 5);
    const auto n = w.finish();
    if (n && transport_) {
      sub.sent = now;
      if (transport_->sendHud(sub.endpoint, tx_, n)) {
        sub.revision = liveRevision_;
        sub.retryDelayMs = 0;
      } else {
        sub.retryDelayMs = sub.retryDelayMs ? std::min<unsigned>(250, sub.retryDelayMs * 2) : 20;
      }
    }
  }
}
void Hud::service(uint32_t at) {
  now = at;
  if (!isHud()) {
    publishLive();
    for (unsigned i = 0; i < 2; ++i) {
      auto &l = leases_[i];
      if (!l.active)
        continue;
      if (l.epoch != controller_.motionEpoch[i])
        l.active = false;
      else if (uint32_t(now - l.renewed) >= LeaseMs) {
        controller_.stopOwnedJog(i, l.epoch, now);
        l.active = false;
      }
    }
    return;
  }
  if (!client_) {
    invalidateMirror();
    return;
  }
  if (!available()) {
    releaseManual();
    invalidateMirror();
  }
  if (auto *p = active();
      p && !paced(p->endpoint) && !p->manual && p->fresh(now) && uint32_t(now - p->capabilityAt) >= 500) {
    Writer w{tx_, sizeof tx_};
    const auto seq = nextSerial();
    header(w, {Kind::ExtendedQuery, id, p->id, boot, p->boot, seq, p->remoteTick, p->wireVersion, p->state.rangeRevision});
    const auto n = w.finish();
    p->capabilitySerial = seq;
    p->capabilityAt = now;
    for (unsigned i = 11; i > 0; --i) {
      p->querySerial[i] = p->querySerial[i - 1];
      p->queryAt[i] = p->queryAt[i - 1];
    }
    p->querySerial[0] = seq;
    p->queryAt[0] = now;
    if (n && transport_) {
      transport_->sendHud(p->endpoint, tx_, n);
    }
  }
  auto &manual = client_->manual;
  if (manual.used) {
    if (!manual.command.release &&
        (uint32_t(now - client_->manualInputAt) >= std::clamp<unsigned>(manual.command.inputLeaseMs,100,1000) || manual.selection != selectionEpoch)) {
#if CROWPANEL_TOUCH_TRACE
      printf("[TouchLease] t=%lu gap=%lu gesture=%lu selection=%d\n", (unsigned long)now,
             (unsigned long)(now - client_->manualInputAt), (unsigned long)manual.command.gesture,
             manual.selection != selectionEpoch);
#endif
      releaseManual();
    }
    if (uint32_t(now - manual.started) >= 1000 && manual.command.release) {
      manual.used = false;
    } else if (uint32_t(now - manual.sent) >= (manual.command.release ? 100u : manualIntervalMs(manual.endpoint))) {
      sendManual(manual);
    }
  }
  for (auto &j : client_->jobs)
    if (j.used) {
      if (uint32_t(now - j.started) >= (paced(j.endpoint)?responseMs(j.endpoint)*3:1000u)) {
        ++timeouts;
        finish(j, Result::Timeout);
      } else if (!j.attempts || (j.attempts < 3 && uint32_t(now - j.sent) >= (paced(j.endpoint)?responseMs(j.endpoint):150u)))
        sendJob(j);
    }
  for (unsigned i = 0; i < 2; ++i)
    if (client_->held[i].active && uint32_t(now - client_->held[i].sent) >= 250)
      sendRenew(i);
  if (auto *activePeer = active(); activePeer && !paced(activePeer->endpoint) && activePeer->fresh(now) &&
      uint32_t(now - activePeer->subscribeSent) >= 250) subscribe(*activePeer);
  // At most 20 state requests/s, active 10Hz and fair round-robin for every other saved peer.
  if (uint32_t(now - lastPoll_) < 50)
    return;
  auto *p = active();
  if (p && p->saved && uint32_t(now - selectedPoll_) >= (paced(p->endpoint)?config.link.intervalMs:p->live && uint32_t(now - p->liveAt) < 1000 ? 1000u : 100u)) {
    sendQuery(p->endpoint, p->id);
    p->lastPoll = selectedPoll_ = lastPoll_ = now;
    return;
  }
  auto *start = peer(pollCursor_);
  auto *cursor = start && start->next ? start->next.get() : peers_.get();
  const auto *first = cursor;
  while (cursor) {
    if (cursor->saved && cursor->id != config.selected && uint32_t(now - cursor->lastPoll) >= (paced(cursor->endpoint)?std::max<unsigned>(1000,config.link.intervalMs):1000u)) {
      sendQuery(cursor->endpoint, cursor->id);
      cursor->lastPoll = lastPoll_ = now;
      pollCursor_ = cursor->id;
      return;
    }
    cursor = cursor->next ? cursor->next.get() : peers_.get();
    if (cursor == first)
      break;
  }
}
bool Hud::writePeers(JsonWriter &j, size_t offset, size_t limit, bool discovered) const {
  j << "{\"total\":" << count(!discovered) << ",\"offset\":" << offset << ",\"items\":[";
  size_t n = 0, shown = 0;
  for (auto *p = peers_.get(); p; p = p->next.get()) {
    if (p->saved == discovered)
      continue;
    if (n++ < offset)
      continue;
    if (shown == limit)
      break;
    if (shown++)
      j << ',';
    char node[17];
    idText(node, p->id);
    const uint32_t address = p->endpoint.address;
    j << "{\"id\":" << jsonQuoted(node) << ",\"name\":" << jsonQuoted(p->label)
      << ",\"transport\":" << jsonQuoted(p->endpoint.uart ? "uart" : "wifi") << ",\"address\":\""
      << (address & 255) << '.' << ((address >> 8) & 255) << '.' << ((address >> 16) & 255) << '.'
      << ((address >> 24) & 255) << "\",\"port\":" << p->endpoint.port
      << ",\"selected\":" << (p->id == config.selected ? "true" : "false") << ",\"online\":"
      << (p->fresh(now, std::max<uint32_t>(3000, uint32_t(count() * 300))) ? "true" : "false")
      << ",\"ageMs\":";
    if (p->received)
      j << uint32_t(now - p->receivedAt);
    else
      j << "null";
    j << ",\"az\":";
    if (p->received)
      j << p->state.axes[0].current;
    else
      j << "null";
    j << ",\"el\":";
    if (p->received)
      j << p->state.axes[1].current;
    else
      j << "null";
    j << ",\"azCalibrated\":" << (p->received && p->state.axes[0].calibrated ? "true" : "false")
      << ",\"elCalibrated\":" << (p->received && p->state.axes[1].calibrated ? "true" : "false")
      << ",\"hasEl\":" << (p->state.model == Model::G5500 ? "true" : "false")
      << ",\"moving\":" << ((p->state.axes[0].direction || p->state.axes[1].direction) ? "true" : "false")
      << '}';
  }
  j << "]}";
  return j.good();
}
void Hud::writeStatus(JsonWriter &j, bool live) const {
  char node[17], selected[17];
  idText(node, id);
  idText(selected, config.selected);
  j << ",\"deviceRole\":" << jsonQuoted(isHud() ? "hud" : "controller")
    << ",\"deviceId\":" << jsonQuoted(node) << ",\"hudPort\":" << config.port
    << ",\"hudUart\":" << (config.uart ? "true" : "false") << ",\"hudWifi\":" << unsigned(config.wifi)
    << ",\"hudBaud\":" << config.link.baud << ",\"hudIntervalMs\":" << config.link.intervalMs
    << ",\"hudReadyHigh\":" << unsigned(config.link.readyHigh)
    << ",\"hudSelected\":" << jsonQuoted(selected) << ",\"hudSelectionEpoch\":" << selectionEpoch
    << ",\"hudOnline\":" << (available() ? "true" : "false") << ",\"hudAgeMs\":";
  if (freshnessAge() != UINT32_MAX)
    j << (live ? (freshnessAge() / 250) * 250 : freshnessAge());
  else
    j << "null";
  auto *p = active();
  j << ",\"hudLive\":" << (p && p->live && uint32_t(now-p->liveAt)<1000 ? "true" : "false");
  if(p && !p->endpoint.uart) {const auto a=p->endpoint.address;char ip[16];std::snprintf(ip,sizeof ip,"%u.%u.%u.%u",unsigned(a&255),unsigned((a>>8)&255),unsigned((a>>16)&255),unsigned(a>>24));
    j << ",\"hudHost\":" << jsonQuoted(ip);}
  j << ",\"hudName\":" << jsonQuoted(p ? p->label : "")
    << ",\"hudTransport\":" << jsonQuoted(p ? (p->endpoint.uart ? "uart" : "wifi") : "")
    << ",\"hudPeers\":" << count() << ",\"remoteDrive\":";
  if (p && p->received)
    j << jsonQuoted(p->state.drive == 0   ? "YAESU"
                    : p->state.drive == 1 ? "SEPARATE_PWM"
                    : p->state.drive == 2 ? "BTS7960"
                    : p->state.drive == 3 ? "SIMULATED"
                                          : "NONE");
  else
    j << "null";
  j << ",\"remoteBoard\":";
  if (p && p->received)
    j << unsigned(p->state.board);
  else
    j << "null";
  j << ",\"hudCatalogError\":" << (catalogError ? "true" : "false")
    << ",\"hudRejected\":" << rejected << ",\"hudTimeouts\":" << timeouts << ",\"hudBusy\":" << busy
    << ",\"hudReceived\":" << received << ",\"hudSent\":" << sent
    << ",\"hudSendErrors\":" << sendErrors << ",\"hudSendErrno\":" << sendErrno;
}
} // namespace forkrot
