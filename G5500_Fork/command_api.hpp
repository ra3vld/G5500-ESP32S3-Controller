// Bounded management protocol shared by every firmware profile. GPL-3.0.
#pragma once
#include "api.hpp"
#include "form_decode.hpp"
#include <array>
#include <memory>
namespace forkrot {
inline constexpr size_t COMMAND_INPUT_MAX = 1024, COMMAND_REPLY_MAX = 1200, COMMAND_PAGE_MAX = 672;
template<size_t Capacity> struct CommandFormStorage {
  char bytes[Capacity]{};
  std::array<FormArgument, 96> entries;
  size_t count = 0;
  bool parse(std::string_view text, size_t limit = COMMAND_INPUT_MAX) {
    return decodeForm(text,bytes,sizeof bytes,entries.data(),entries.size(),count,limit);
  }
  std::string_view get(std::string_view name) const {
    for(size_t i=0;i<count;++i)if(entries[i].first==name)return entries[i].second;
    return {};
  }
  FormView view() const { return {entries.data(), count}; }
};
using CommandForm = CommandFormStorage<1537>; // larger hardware HTTP form
class CommandServer {
public:
  using Dispatch = void (*)(void *, std::string_view, std::string_view, FormView, JsonWriter &, int &, const char *&);
  using AdmitAllocation = bool (*)(size_t);
  void begin(uint64_t boot, uint32_t seed, Dispatch dispatch, void *context, const uint32_t *selection = nullptr,
             AdmitAllocation admit = nullptr);
  size_t process(uint64_t peer, std::string_view request, uint32_t now, char *reply, size_t capacity);
private:
  struct Session {
    uint64_t peer = 0;
    uint32_t client = 0, sid = 0, cid = 0, touched = 0, fingerprint = 0;
    size_t size = 0;
    std::unique_ptr<char[]> reply;
  };
  std::array<Session, 4> sessions_;
  // Requests execute serially on the controller loop. Keep the decoder out of
  // the nested UDP -> dispatcher -> settings stack on boards without PSRAM.
  CommandFormStorage<COMMAND_INPUT_MAX+1> request_;
  uint64_t boot_ = 0;
  uint32_t nextSession_ = 0;
  Dispatch dispatch_ = nullptr;
  void *context_ = nullptr;
  const uint32_t *selection_ = nullptr;
  AdmitAllocation admit_ = nullptr;
};
}
