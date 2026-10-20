// Bounded management protocol shared by every firmware profile. GPL-3.0.
#pragma once
#include "api.hpp"
#include <array>
namespace forkrot {
inline constexpr size_t COMMAND_INPUT_MAX = 1024, COMMAND_REPLY_MAX = 1200, COMMAND_PAGE_MAX = 672;
struct CommandForm {
  char bytes[COMMAND_INPUT_MAX + 1]{};
  std::array<FormArgument, 40> entries;
  size_t count = 0;
  bool parse(std::string_view text);
  std::string_view get(std::string_view name) const;
  FormView view() const { return {entries.data(), count}; }
};
class CommandServer {
public:
  using Dispatch = void (*)(void *, std::string_view, std::string_view, FormView, JsonWriter &, int &, const char *&);
  void begin(uint64_t boot, uint32_t seed, Dispatch dispatch, void *context, const uint32_t *selection = nullptr);
  size_t process(uint64_t peer, std::string_view request, uint32_t now, char *reply, size_t capacity);
private:
  struct Session {
    uint64_t peer = 0;
    uint32_t client = 0, sid = 0, cid = 0, touched = 0, fingerprint = 0;
    size_t size = 0;
    char reply[COMMAND_REPLY_MAX + 1]{};
  };
  std::array<Session, 4> sessions_;
  // Requests execute serially on the controller loop. Keep the decoder out of
  // the nested UDP -> dispatcher -> settings stack on boards without PSRAM.
  CommandForm request_;
  uint64_t boot_ = 0;
  uint32_t nextSession_ = 0;
  Dispatch dispatch_ = nullptr;
  void *context_ = nullptr;
  const uint32_t *selection_ = nullptr;
};
}
