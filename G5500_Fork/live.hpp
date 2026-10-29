// Bounded live protocol shared by firmware and host tests. GPL-3.0.
#pragma once
#include "runtime.hpp"
#include "http_request_gate.hpp"
namespace forkrot {
inline constexpr size_t LIVE_INPUT_CAPACITY = 512, LIVE_STATE_CAPACITY = 3072;
// v2 wire layout is explicit little-endian, never a dumped C++ struct.
inline constexpr size_t LIVE_BINARY_SIZE = 160;
struct LiveRequest {
  char bytes[LIVE_INPUT_CAPACITY + 1]{};
  FormArgument args[16];
  size_t count = 0;
  uint32_t id = 0, selection = 0;
  uint64_t boot = 0;
  std::string_view path;
  bool parse(std::string_view);
  std::string_view get(std::string_view) const;
  bool stop() const { const auto d = get("d"); return path == "/api/move" && (d == "S" || d == "A" || d == "E"); }
};
void liveBootText(char (&out)[17], uint64_t boot);
Response executeLive(const LiveRequest &, Controller &, Protocol &, Settings &, Hud &, uint32_t);
uint32_t liveControlFingerprint(const Controller &, const Hud &);
bool writeLiveState(JsonWriter &, const Controller &, const Hud &, uint32_t);
uint32_t liveMetadataFingerprint(const Controller &, const Hud &);
bool writeLiveMetadata(JsonWriter &, const Controller &, const Hud &);
void writeLiveBinary(char *, const Controller &, const Hud &, uint32_t now,
                     uint32_t revision, uint32_t metadataRevision, bool memoryLimited);
}
