// Waveshare ESP32-S3-ETH, ESP32-S3R8 / 16 MB flash / W5500. GPL-3.0.
#pragma once
namespace forkrot::board {
inline constexpr const char *NAME = "Waveshare ESP32-S3-ETH / W5500";
// Camera + dual BTS7960: the four microSD signals are repurposed for ADC/EN.
// GPIO4..7 require connections at the card-slot pads; remove the microSD card.
inline constexpr int PIN_AZ_ADC=4, PIN_EL_ADC=5;
inline constexpr int PIN_LEFT=16, PIN_RIGHT=17, PIN_DOWN=43, PIN_UP=44;
inline constexpr int PIN_SPEED=6, PIN_EL_SPEED=7;
inline constexpr int PIN_UART_RX=-1, PIN_UART_TX=-1;
inline constexpr int PIN_HC05_RX=-1, PIN_HC05_TX=-1, PIN_HC05_STATE=-1;
inline constexpr int PIN_RGB=21, PIN_LED=-1;
inline constexpr int PIN_TFT_MOSI=-1, PIN_TFT_SCK=-1, PIN_TFT_MISO=-1;
inline constexpr int PIN_TFT_CS=-1, PIN_TFT_DC=-1, PIN_TFT_RST=-1;
inline constexpr int PIN_TOUCH_CS=-1, PIN_TOUCH_IRQ=-1;
inline constexpr int PIN_LCD_RS=-1, PIN_LCD_E=-1, PIN_LCD_D4=-1, PIN_LCD_D5=-1, PIN_LCD_D6=-1, PIN_LCD_D7=-1;
inline constexpr int ETH_CS=14, ETH_IRQ=10, ETH_RESET=9;
inline constexpr int ETH_SCK=13, ETH_MISO=12, ETH_MOSI=11;
inline constexpr int CAM_POWER=8, CAM_VSYNC=1, CAM_HREF=2, CAM_XCLK=3, CAM_PCLK=39;
inline constexpr int CAM_SDA=48, CAM_SCL=47;
inline constexpr int CAM_DATA[]={41,45,46,42,40,38,15,18}; // D0..D7
constexpr bool ethernetPin(int n) { return n>=9 && n<=14; }
constexpr bool cameraPin(int n) {
  if(n==CAM_POWER) return true; // Power gate remains owned even when camera is disabled.
#if CAMERA_ENABLED
  if(n==CAM_VSYNC || n==CAM_HREF || n==CAM_XCLK || n==CAM_PCLK || n==CAM_SDA || n==CAM_SCL) return true;
  for(int pin:CAM_DATA) if(n==pin) return true;
#endif
  return false;
}
} // namespace forkrot::board
