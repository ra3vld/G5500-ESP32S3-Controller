// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
namespace forkrot { namespace board {
// Shared WROOM/WROVER wiring: do not use flash GPIO6..11 or PSRAM GPIO16/17.
inline constexpr const char *NAME = "ESP32 WROOM/WROVER DevKit";
inline constexpr int PIN_AZ_ADC = 34, PIN_EL_ADC = 35;
inline constexpr int PIN_LEFT = 25, PIN_RIGHT = 26, PIN_DOWN = 14, PIN_UP = 27;
inline constexpr int PIN_SPEED = 32, PIN_EL_SPEED = 33;
inline constexpr int PIN_UART_RX = 3, PIN_UART_TX = 1;
// Separate UART1; compatible with all three display profiles and WROVER PSRAM.
// STATE on GPIO15 can suppress the ROM boot log while LOW (MTDO strap).
inline constexpr int PIN_HC05_RX = 39, PIN_HC05_TX = 5, PIN_HC05_STATE = 15;
inline constexpr int PIN_RGB = -1, PIN_LED = -1;
inline constexpr int PIN_TFT_MOSI = 23, PIN_TFT_SCK = 18, PIN_TFT_MISO = 19;
inline constexpr int PIN_TFT_CS = 21, PIN_TFT_DC = 22, PIN_TFT_RST = 4;
inline constexpr int PIN_TOUCH_CS = 13, PIN_TOUCH_IRQ = 36;
inline constexpr int PIN_LCD_RS = 21, PIN_LCD_E = 22;
inline constexpr int PIN_LCD_D4 = 23, PIN_LCD_D5 = 18, PIN_LCD_D6 = 19, PIN_LCD_D7 = 4;
} }
