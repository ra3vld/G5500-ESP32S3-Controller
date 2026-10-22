// Wireless-Tag WT32-ETH01 with LAN8720A; GPL-3.0.
#pragma once
namespace forkrot::board {
inline constexpr const char *NAME = "WT32-ETH01 / LAN8720";
inline constexpr int PIN_AZ_ADC=36, PIN_EL_ADC=35;
inline constexpr int PIN_LEFT=32, PIN_RIGHT=33, PIN_DOWN=14, PIN_UP=15;
inline constexpr int PIN_SPEED=4, PIN_EL_SPEED=2;
inline constexpr int PIN_UART_RX=3, PIN_UART_TX=1;
inline constexpr int PIN_HC05_RX=39, PIN_HC05_TX=17, PIN_HC05_STATE=5;
inline constexpr int PIN_RGB=-1, PIN_LED=-1;
inline constexpr int PIN_TFT_MOSI=-1, PIN_TFT_SCK=-1, PIN_TFT_MISO=-1;
inline constexpr int PIN_TFT_CS=-1, PIN_TFT_DC=-1, PIN_TFT_RST=-1;
inline constexpr int PIN_TOUCH_CS=-1, PIN_TOUCH_IRQ=-1;
inline constexpr int PIN_LCD_RS=-1, PIN_LCD_E=-1, PIN_LCD_D4=-1, PIN_LCD_D5=-1, PIN_LCD_D6=-1, PIN_LCD_D7=-1;
inline constexpr int ETH_ADDRESS=1, ETH_POWER=16, ETH_MDC=23, ETH_MDIO=18;
constexpr bool ethernetPin(int pin) {
  return pin==0 || pin==16 || pin==18 || pin==19 || pin==21 || pin==22 ||
         pin==23 || pin==25 || pin==26 || pin==27;
}
} // namespace forkrot::board
