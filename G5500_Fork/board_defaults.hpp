// Compile-time wiring defaults; runtime assignments live in HardwareConfig. GPL-3.0.
#pragma once
#include "build_config.hpp"
#if SELECTED_BOARD == BOARD_S3
#include "board_s3.hpp"
#elif SELECTED_BOARD == BOARD_S2_LEGACY
#include "board_s2_legacy.hpp"
#elif SELECTED_BOARD == BOARD_S2_MINI
#include "board_s2_mini.hpp"
#elif SELECTED_BOARD == BOARD_WT32_ETH01
#include "board_wt32_eth01.hpp"
#elif SELECTED_BOARD == BOARD_ESP32
#include "board_esp32.hpp"
#elif SELECTED_BOARD == BOARD_CROWPANEL_21
#include "board_crowpanel_21.hpp"
// The built-in RGB panel has no external TFT/LCD wiring defaults.
namespace forkrot::board {
inline constexpr int PIN_TFT_MOSI=-1, PIN_TFT_SCK=-1, PIN_TFT_MISO=-1;
inline constexpr int PIN_TFT_CS=-1, PIN_TFT_DC=-1, PIN_TFT_RST=-1;
inline constexpr int PIN_TOUCH_CS=-1, PIN_TOUCH_IRQ=-1;
inline constexpr int PIN_LCD_RS=-1, PIN_LCD_E=-1, PIN_LCD_D4=-1, PIN_LCD_D5=-1, PIN_LCD_D6=-1, PIN_LCD_D7=-1;
}
#else
#error "Unknown board profile"
#endif
