// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include "control.hpp"

namespace forkrot {
// Bind a confirmation to the command that was actually presented on the panel.
// Current coordinates are deliberately excluded: telemetry must not cancel a tap.
struct PanelCommandContext {
  Model model = Model::G5500;
  Mode mode = Mode::Manual;
  uint32_t rangeRevision = 0, selectionEpoch = 0;
  unsigned axis = 0;
  Target target{};
  bool pending = false, driveError = false;

  bool sameScope(const PanelCommandContext &other) const {
    return model == other.model && mode == other.mode && rangeRevision == other.rangeRevision &&
           selectionEpoch == other.selectionEpoch && axis == other.axis;
  }
  bool operator==(const PanelCommandContext &other) const {
    return sameScope(other) && pending == other.pending && driveError == other.driveError && target.hasAz == other.target.hasAz &&
           target.hasEl == other.target.hasEl && (!target.hasAz || target.az == other.target.az) &&
           (!target.hasEl || target.el == other.target.el);
  }
};
}
