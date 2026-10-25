// Display/input diagnostics, read on the same loop as the panel. GPL-3.0.
#pragma once
#include <cstdint>
namespace forkrot {
struct PanelMetrics {
  uint32_t renderMaxUs=0,touchReads=0,touchErrors=0;
  bool touchHealthy=false,touchDown=false;
  uint16_t touchX=0,touchY=0;
};
inline PanelMetrics panelMetrics;
}
