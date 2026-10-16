#pragma once
#include "build_config.hpp"
#if SELECTED_BOARD == BOARD_CROWPANEL_21
#include <cstdint>
#include <esp_lcd_panel_ops.h>
namespace forkrot::crowpanel {
bool beginBus();
bool readButton(bool &down);
bool readTouch(uint16_t &x, uint16_t &y);
void beginEncoder();
int encoderDelta();
esp_lcd_panel_handle_t beginPanel();
struct DisplayStats { uint32_t vsyncs, frames, waitMaxUs, timeouts; bool failed; };
DisplayStats displayStats();
}
#endif
