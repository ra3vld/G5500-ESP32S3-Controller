// Generic ESP32-C3, 4 MB Flash, native USB, no display. GPL-3.0.
#pragma once
namespace forkrot::board {
inline constexpr const char *NAME = "ESP32-C3";
// Assign the connected drive and feedback through the hardware settings.
inline constexpr int PIN_AZ_ADC=-1, PIN_EL_ADC=-1;
inline constexpr int PIN_LEFT=-1, PIN_RIGHT=-1, PIN_DOWN=-1, PIN_UP=-1;
inline constexpr int PIN_SPEED=-1, PIN_EL_SPEED=-1, PIN_RGB=-1, PIN_LED=-1;
inline constexpr int PIN_UART_RX=20, PIN_UART_TX=21;
inline constexpr int PIN_HC05_RX=-1, PIN_HC05_TX=-1, PIN_HC05_STATE=-1;
inline constexpr int PIN_TFT_MOSI=-1, PIN_TFT_SCK=-1, PIN_TFT_MISO=-1;
inline constexpr int PIN_TFT_CS=-1, PIN_TFT_DC=-1, PIN_TFT_RST=-1;
inline constexpr int PIN_TOUCH_CS=-1, PIN_TOUCH_IRQ=-1;
inline constexpr int PIN_LCD_RS=-1, PIN_LCD_E=-1, PIN_LCD_D4=-1, PIN_LCD_D5=-1, PIN_LCD_D6=-1, PIN_LCD_D7=-1;
}
