// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
namespace forkrot { namespace board {
inline constexpr int PIN_AZ_ADC = 4, PIN_EL_ADC = 5;
inline constexpr int PIN_TFT_MOSI = 10, PIN_TFT_SCK = 11, PIN_TFT_MISO = 2;
inline constexpr int PIN_TFT_CS = 12, PIN_TFT_DC = 13, PIN_TFT_RST = 14;
inline constexpr int PIN_TOUCH_CS = 8, PIN_TOUCH_IRQ = 9;
inline constexpr int PIN_LCD_RS = 8, PIN_LCD_E = 9;
inline constexpr int PIN_LCD_D4 = 10, PIN_LCD_D5 = 11, PIN_LCD_D6 = 12, PIN_LCD_D7 = 13;
inline constexpr int PIN_HC05_STATE = 3, PIN_HC05_RX = 6, PIN_HC05_TX = 7;
inline constexpr const char *NAME = "S3 DevKitC-1 N16R8";
inline constexpr int PIN_LEFT = 15, PIN_RIGHT = 16, PIN_DOWN = 18, PIN_UP = 17;
inline constexpr int PIN_SPEED = 21, PIN_EL_SPEED = 47;
inline constexpr int PIN_UART_RX = 44, PIN_UART_TX = 43;
inline constexpr int PIN_RGB = 48, PIN_LED = -1;
} }
