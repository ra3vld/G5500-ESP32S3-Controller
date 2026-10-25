// Resumable commissioning state. No radio or GPIO dependencies. GPL-3.0.
#pragma once
#include <array>
#include <cstdint>
namespace forkrot {
enum class SetupStep : uint8_t { Network, Role, Model, Equipment, Inputs, Calibration, Tuning, Programs, Peers, Review, Done };
struct SetupState {
  bool required = false, paused = false, equipment = false, fresh = false, pendingNext = false;
  int8_t drive = -1; // Unapplied drive choice; -1 inherits the hardware record.
  bool simulation = false;
  SetupStep step = SetupStep::Done;
  uint16_t checked = 0;
  uint32_t generation = 0;
  SetupStep next(bool hud, bool simulation) const {
    switch(step) {
      case SetupStep::Network: return SetupStep::Role;
      case SetupStep::Role: return hud ? SetupStep::Equipment : SetupStep::Model;
      case SetupStep::Model: return SetupStep::Equipment;
      case SetupStep::Equipment: return SetupStep::Inputs;
      case SetupStep::Inputs: return hud ? SetupStep::Peers : simulation ? SetupStep::Programs : SetupStep::Calibration;
      case SetupStep::Calibration: return SetupStep::Tuning;
      case SetupStep::Tuning: return SetupStep::Programs;
      case SetupStep::Peers: return SetupStep::Programs;
      case SetupStep::Programs: return SetupStep::Review;
      default: return SetupStep::Done;
    }
  }
};
using SetupBytes = std::array<uint8_t,16>;
inline SetupBytes encodeSetup(const SetupState &s) {
  SetupBytes b{2,uint8_t(s.required),uint8_t(s.paused),uint8_t(s.equipment),uint8_t(s.step),uint8_t(s.checked),uint8_t(s.checked>>8),uint8_t(s.fresh | (s.pendingNext<<1) | ((s.drive+1)<<2) | (s.simulation<<5))};
  for(unsigned i=0;i<4;++i)b[8+i]=uint8_t(s.generation>>(8*i));
  uint32_t hash=2166136261u;for(unsigned i=0;i<12;++i)hash=(hash^b[i])*16777619u;
  for(unsigned i=0;i<4;++i)b[12+i]=uint8_t(hash>>(8*i));
  return b;
}
inline bool decodeSetup(const SetupBytes &b, SetupState &s) {
  if((b[0]!=1 && b[0]!=2) || b[1]>1 || b[2]>1 || b[3]>1 || b[4]>uint8_t(SetupStep::Done))return false;
  const unsigned mask=b[0]==1?3:7,simBit=b[0]==1?16:32;
  const unsigned drive=(b[7]>>2)&mask;
  if(b[7]>((simBit<<1)-1) || drive>(b[0]==1?3u:4u) || ((b[7]&simBit)&&!drive))return false;
  uint32_t hash=2166136261u;for(unsigned i=0;i<12;++i)hash=(hash^b[i])*16777619u;
  for(unsigned i=0;i<4;++i)if(b[12+i]!=uint8_t(hash>>(8*i)))return false;
  SetupState n;n.required=b[1];n.paused=b[2];n.equipment=b[3];n.fresh=b[7]&1;n.pendingNext=b[7]&2;n.step=SetupStep(b[4]);n.checked=b[5]|uint16_t(b[6])<<8;
  n.drive=int8_t(drive)-1;n.simulation=b[7]&simBit;
  for(unsigned i=0;i<4;++i)n.generation|=uint32_t(b[8+i])<<(8*i);
  s=n;return true;
}
}
