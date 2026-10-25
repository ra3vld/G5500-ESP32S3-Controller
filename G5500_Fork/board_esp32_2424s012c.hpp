// Guition ESP32-2424S012C / ESP32-2424S012C-Y(B), ESP32-C3, 4 MB. GPL-3.0.
#pragma once
namespace forkrot::board {
inline constexpr const char *NAME = "ESP32-2424S012C";
// Local wiring is assigned at runtime; no motor or feedback pins by default.
inline constexpr int PIN_AZ_ADC=-1, PIN_EL_ADC=-1;
inline constexpr int PIN_LEFT=-1, PIN_RIGHT=-1, PIN_DOWN=-1, PIN_UP=-1;
inline constexpr int PIN_SPEED=-1, PIN_EL_SPEED=-1, PIN_RGB=-1, PIN_LED=-1;
// SH1.0 connector: native UART0, 3.3 V levels.
inline constexpr int PIN_UART_RX=20, PIN_UART_TX=21;
inline constexpr int PIN_HC05_RX=-1, PIN_HC05_TX=-1, PIN_HC05_STATE=-1;
inline constexpr int PIN_PANEL_MOSI=7, PIN_PANEL_SCK=6, PIN_PANEL_CS=10, PIN_PANEL_DC=2;
inline constexpr int PIN_BACKLIGHT=3;
inline constexpr int PIN_I2C_SDA=4, PIN_I2C_SCL=5, PIN_CST_RST=1, PIN_CST_IRQ=0;
inline constexpr int PIN_TFT_MOSI=-1, PIN_TFT_SCK=-1, PIN_TFT_MISO=-1;
inline constexpr int PIN_TFT_CS=-1, PIN_TFT_DC=-1, PIN_TFT_RST=-1;
inline constexpr int PIN_TOUCH_CS=-1, PIN_TOUCH_IRQ=-1;
inline constexpr int PIN_LCD_RS=-1, PIN_LCD_E=-1, PIN_LCD_D4=-1, PIN_LCD_D5=-1, PIN_LCD_D6=-1, PIN_LCD_D7=-1;
}
