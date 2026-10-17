// HUD/1 portable transport, identity, command lifetime and mirrors. GPL-3.0.
#include "hud.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <new>
namespace forkrot {
namespace {
constexpr uint32_t TicketMs = 2000, LeaseMs = 1000;
enum class Kind : uint8_t { Discover = 1, Query, State, Command, Ack, Renew };
struct Header {
  Kind kind{};
  uint64_t source = 0, target = 0, boot = 0, targetBoot = 0;
  uint32_t serial = 0, ticket = 0;
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
  for (char c : std::string_view("HUD1"))
    w.u8(c);
  w.u8(uint8_t(h.kind));
  w.u64(h.source);
  w.u64(h.target);
  w.u64(h.boot);
  w.u64(h.targetBoot);
  w.u32(h.serial);
  w.u32(h.ticket);
}
bool header(Reader &r, Header &h) {
  for (char c : std::string_view("HUD1"))
    if (r.u8() != uint8_t(c))
      r.ok = false;
  h.kind = Kind(r.u8());
  h.source = r.u64();
  h.target = r.u64();
  h.boot = r.u64();
  h.targetBoot = r.u64();
  h.serial = r.u32();
  h.ticket = r.u32();
  return r.ok && h.source && h.boot && uint8_t(h.kind) >= 1 && uint8_t(h.kind) <= 6;
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
  w.u8(c.external);
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
    break;
  }
}
bool command(Reader &r, ControlCommand &c) {
  c.op = ControlOp(r.u8());
  auto external = r.u8();
  c.external = external;
  if (external > 1)
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
    c.axis = int(r.u8()) - 1;
    if (c.axis < -1 || c.axis > 1)
      return false;
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
    break;
  default:
    return false;
  }
  return r.done();
}
void state(Writer &w, const HudState &s) {
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
  w.text(s.name);
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
    const auto &v = s.speeds[i];
    for (unsigned n : {v.speedPercent, v.startSpeedPercent, v.approachSpeedPercent, v.accelerationMs,
                       v.decelerationMs, v.slowdownDegrees})
      w.u32(n);
  }
  for (const auto &p : s.presets) {
    w.text(p.label);
    target(w, p.target);
  }
}
bool state(Reader &r, HudState &s) {
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
  r.text(s.name, sizeof s.name);
  r.text(s.fault, sizeof s.fault);
  if (s.model > Model::G800DXA || s.mode > Mode::AutoHour || (s.maximum != 360 && s.maximum != 450) ||
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
    auto &v = s.speeds[i];
    v.speedPercent = r.u32();
    v.startSpeedPercent = r.u32();
    v.approachSpeedPercent = r.u32();
    v.accelerationMs = r.u32();
    v.decelerationMs = r.u32();
    v.slowdownDegrees = r.u32();
    if (f > 15 || std::abs(a.direction) > 1 || std::abs(a.wanted) > 1 || a.current < 0 ||
        a.current > (i ? 180 : s.maximum) || a.speedOutputPercent < 0 || a.speedOutputPercent > 100)
      return false;
  }
  for (auto &p : s.presets) {
    char label[49]{};
    r.text(label, sizeof label);
    std::memcpy(p.label, label, sizeof label);
    p.target = target(r);
  }
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
  maximum = c.maximum;
  pending = c.pending;
  lastTarget = c.lastTarget;
  pendingValid = c.pendingValid;
  presetActive = c.presetActive;
  targetKnown = c.targetKnown;
  presetSlot = c.presetSlot;
  speedEnabled = c.speedEnabled;
  driveFault = c.driveFault;
  stopEpoch = c.stopEpoch;
  remaining = c.remaining(now);
  sequenceIndex = uint8_t(c.sequenceIndex());
  sequenceCount = uint8_t(c.sequenceCount());
  drive = (SELECTED_BOARD == BOARD_CROWPANEL_21 && CROWPANEL_SIMULATION) ? 3 : SELECTED_BOARD == BOARD_CROWPANEL_21 ? 4 : DRIVE_INTERFACE;
  board = SELECTED_BOARD;
  std::snprintf(name, sizeof name, "%s", p.rotorName.c_str());
  std::snprintf(fault, sizeof fault, "%s", c.fault.c_str());
  for (unsigned i = 0; i < 2; ++i) {
    axes[i] = c.axes[i];
    speeds[i] = c.speeds[i];
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
  c.maximum = maximum;
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
  config.selected = node;
  ++selectionEpoch;
  protocol_.resetSession();
  invalidateMirror();
  if (auto *p = active(); p && p->received) {
    p->state.apply(controller_, protocol_, p->receivedAt);
    if (!p->fresh(now))
      invalidateMirror();
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
Result Hud::submit(const ControlCommand &c) {
  lastRequest_ = nextSerial();
  auto *p = active();
  lastResult_ = Result::Unavailable;
  if (!available() || !p)
    return lastResult_;
  const bool stop = c.op == ControlOp::Stop || c.op == ControlOp::ReleaseJog;
  Job *slot = nullptr;
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
  } else
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
  slot->command = c;
  slot->peer = p->id;
  slot->boot = p->boot;
  slot->endpoint = p->endpoint;
  slot->id = lastRequest_;
  slot->ticket = p->remoteTick;
  slot->started = now;
  slot->selection = selectionEpoch;
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
bool Hud::sendQuery(const HudEndpoint &e, uint64_t targetId, bool discovery) {
  const uint32_t serial = nextSerial();
  Writer w{tx_, sizeof tx_};
  header(w, {discovery ? Kind::Discover : Kind::Query, id, targetId, boot, 0, serial, 0});
  const auto n = w.finish();
  if (!transport_ || !n || !transport_->sendHud(e, tx_, n))
    return false;
  if (discovery)
    scanSerial_ = serial;
  else if (auto *p = peer(targetId)) {
    p->querySerial[1] = p->querySerial[0];
    p->queryAt[1] = p->queryAt[0];
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
                    uint64_t targetBoot) {
  Writer w{tx_, sizeof tx_};
  header(w, {ack ? Kind::Ack : Kind::State, id, targetId, boot, targetBoot, seq, now});
  if (ack)
    w.u8(uint8_t(result));
  HudState s;
  s.capture(controller_, protocol_, now);
  state(w, s);
  auto n = w.finish();
  return transport_ && n && transport_->sendHud(e, tx_, n);
}
bool Hud::sendJob(Job &j) {
  Writer w{tx_, sizeof tx_};
  header(w, {Kind::Command, id, j.peer, boot, j.boot, j.id, j.ticket});
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
  header(w, {Kind::Renew, id, h.peer, boot, h.boot, h.command, p->remoteTick});
  w.u8(uint8_t(axis));
  auto n = w.finish();
  if (transport_ && n && transport_->sendHud(h.endpoint, tx_, n))
    h.sent = now;
}
Result Hud::execute(const ControlCommand &c, uint64_t source, uint64_t session, uint32_t seq) {
  Result r = Result::Invalid;
  switch (c.op) {
  case ControlOp::Target:
    r = controller_.target(c.target, c.external, now);
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
    r = controller_.sequence(c.points.data(), c.count, c.interval, now);
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
  if (h.kind == Kind::State || h.kind == Kind::Ack) {
    if (!isHud() || !client_ || h.target != id || h.targetBoot != boot)
      return;
    Result result = Result::Ok;
    if (h.kind == Kind::Ack) {
      result = Result(r.u8());
      if (result > Result::Cancelled) {
        ++rejected;
        return;
      }
    }
    HudState s;
    if (!state(r, s)) {
      ++rejected;
      return;
    }
    auto *p = peer(h.source);
    uint32_t requestedAt = now;
    bool requested = false;
    const bool discovery =
        h.kind == Kind::State && scanning_ && h.serial == scanSerial_ && uint32_t(now - scanAt_) < 1000 &&
        e.uart == scanEndpoint_.uart &&
        (e.uart || (e.port == scanEndpoint_.port &&
                    (scanEndpoint_.address == 0xffffffffu || e.address == scanEndpoint_.address)));
    if (discovery) {
      requested = true;
      requestedAt = scanAt_;
    }
    if (h.kind == Kind::State && p)
      for (unsigned i = 0; i < 2; ++i)
        if (h.serial == p->querySerial[i] && uint32_t(now - p->queryAt[i]) < 1000) {
          requested = true;
          requestedAt = p->queryAt[i];
        }
    if (h.kind == Kind::Ack)
      for (const auto &job : client_->jobs)
        if (job.used && job.peer == h.source && job.boot == h.boot && job.id == h.serial) {
          requested = true;
          requestedAt = job.started;
          break;
        }
    if (!requested)
      return; // Delayed unsolicited state cannot make an offline drive appear fresh.
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
    if (p->received && (int32_t(h.serial - p->stateSerial) < 0 ||
                        (p->boot == h.boot && int32_t(h.ticket - p->remoteTick) < 0))) {
      if (h.kind == Kind::Ack)
        for (auto &j : client_->jobs)
          if (j.used && j.peer == p->id && j.boot == h.boot && j.id == h.serial) {
            finish(j, result);
            break;
          }
      return;
    }
    if (p->boot && p->boot != h.boot) {
      for (auto &j : client_->jobs)
        if (j.used && j.peer == p->id)
          finish(j, Result::Cancelled);
      for (auto &held : client_->held)
        if (held.peer == p->id)
          held.active = false;
    }
    p->boot = h.boot;
    p->state = std::move(s);
    p->received = true;
    p->receivedAt = requestedAt;
    p->remoteTick = h.ticket;
    p->stateSerial = h.serial;
    if (p->saved && p->id == config.selected)
      p->state.apply(controller_, protocol_, requestedAt);
    if (h.kind == Kind::Ack)
      for (auto &j : client_->jobs)
        if (j.used && j.peer == p->id && j.boot == h.boot && j.id == h.serial) {
          finish(j, result);
          break;
        }
    return;
  }
  if (isHud())
    return; // No HUD-to-HUD forwarding, including accidental loops.
  if (h.kind == Kind::Discover || h.kind == Kind::Query) {
    if (r.done())
      sendState(e, h.source, h.serial, false, Result::Ok, h.boot);
    return;
  }
  if (h.target != id || h.targetBoot != boot || uint32_t(now - h.ticket) >= TicketMs) {
    ++rejected;
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
  if (h.kind != Kind::Command)
    return;
  ControlCommand c;
  if (!command(r, c)) {
    ++rejected;
    return;
  }
  Dedup *free = nullptr;
  for (auto &d : dedup_) {
    if (d.used && uint32_t(now - d.ticket) >= TicketMs)
      d.used = false;
    if (!d.used) {
      if (!free && (&d != &dedup_[0] || c.op == ControlOp::Stop || c.op == ControlOp::ReleaseJog))
        free = &d;
      continue;
    }
    if (d.source == h.source && d.boot == h.boot) {
      if (d.seq == h.serial) {
        ++duplicates;
        sendState(e, h.source, h.serial, true, d.result, h.boot);
        return;
      }
      if (int32_t(h.serial - d.seq) < 0) {
        sendState(e, h.source, h.serial, true, Result::Cancelled, h.boot);
        return;
      }
    }
  }
  const bool stop = c.op == ControlOp::Stop || c.op == ControlOp::ReleaseJog;
  // Reuse this source's previous entry: higher serial makes all older commands stale.
  for (auto &d : dedup_)
    if (d.used && d.source == h.source && d.boot == h.boot) {
      free = &d;
      break;
    }
  if (!free) {
    ++busy;
    sendState(e, h.source, h.serial, true, Result::Busy, h.boot);
    return;
  }
  (void)stop;
  const auto result = execute(c, h.source, h.boot, h.serial);
  *free = {h.source, h.boot, h.serial, h.ticket, result, true};
  sendState(e, h.source, h.serial, true, result, h.boot);
}
void Hud::service(uint32_t at) {
  now = at;
  if (!isHud()) {
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
  if (!available())
    invalidateMirror();
  for (auto &j : client_->jobs)
    if (j.used) {
      if (uint32_t(now - j.started) >= 1000) {
        ++timeouts;
        finish(j, Result::Timeout);
      } else if (!j.attempts || (j.attempts < 3 && uint32_t(now - j.sent) >= 150))
        sendJob(j);
    }
  for (unsigned i = 0; i < 2; ++i)
    if (client_->held[i].active && uint32_t(now - client_->held[i].sent) >= 250)
      sendRenew(i);
  // At most 20 state requests/s, active 10Hz and fair round-robin for every other saved peer.
  if (uint32_t(now - lastPoll_) < 50)
    return;
  auto *p = active();
  if (p && p->saved && uint32_t(now - selectedPoll_) >= 100) {
    sendQuery(p->endpoint, p->id);
    p->lastPoll = selectedPoll_ = lastPoll_ = now;
    return;
  }
  auto *start = peer(pollCursor_);
  auto *cursor = start && start->next ? start->next.get() : peers_.get();
  const auto *first = cursor;
  while (cursor) {
    if (cursor->saved && cursor->id != config.selected && uint32_t(now - cursor->lastPoll) >= 1000) {
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
    j << ",\"moving\":" << ((p->state.axes[0].direction || p->state.axes[1].direction) ? "true" : "false")
      << '}';
  }
  j << "]}";
  return j.good();
}
void Hud::writeStatus(JsonWriter &j) const {
  char node[17], selected[17];
  idText(node, id);
  idText(selected, config.selected);
  j << ",\"deviceRole\":" << jsonQuoted(isHud() ? "hud" : "controller")
    << ",\"deviceId\":" << jsonQuoted(node) << ",\"hudPort\":" << config.port
    << ",\"hudUart\":" << (config.uart ? "true" : "false") << ",\"hudWifi\":" << unsigned(config.wifi)
    << ",\"hudSelected\":" << jsonQuoted(selected) << ",\"hudSelectionEpoch\":" << selectionEpoch
    << ",\"hudOnline\":" << (available() ? "true" : "false") << ",\"hudAgeMs\":";
  if (freshnessAge() != UINT32_MAX)
    j << freshnessAge();
  else
    j << "null";
  auto *p = active();
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
  j << ",\"hudCatalogError\":" << (catalogError ? "true" : "false") << ",\"hudRejected\":" << rejected
    << ",\"hudTimeouts\":" << timeouts << ",\"hudBusy\":" << busy;
}
} // namespace forkrot
