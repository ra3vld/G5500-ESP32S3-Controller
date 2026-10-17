// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include "api.hpp"
#include "hud.hpp"
namespace forkrot {
// The single control model plus the last hardware/UI projection. Peripheral
// buffers and settings belong to their respective services, not this object.
struct Runtime {
  Controller controller;
  Protocol protocol{controller};
  Settings loggerSettings;
  Hud hud{controller, protocol};
  float currentAz = 0, currentEl = 0, targetAz = 0, targetEl = 0;
  bool targetValid = false, azTrack = false, elTrack = false;
  uint32_t lastTargetRxMs = 0;
};
} // namespace forkrot
