// G5500 Fork; original by UA1CFM, modifications by RA3VLD. GPL-3.0.
#pragma once
#include <Arduino.h>
#if ARDUINO_USB_MODE || ARDUINO_USB_CDC_ON_BOOT
#define FORK_NATIVE_USB 1
#else
#define FORK_NATIVE_USB 0
#endif
#if CONFIG_IDF_TARGET_ESP32S2
#define FORK_CHIP_NAME "ESP32-S2"
#define FORK_USB_NAME "CDC"
#else
#define FORK_CHIP_NAME "ESP32-S3"
#define FORK_USB_NAME "Serial/JTAG"
#endif
namespace forkrot {
namespace board {
inline constexpr int PIN_CONFIRM = 0, PIN_SPEED = 21;
inline constexpr int PIN_AZ_ADC = 4, PIN_EL_ADC = 5;
inline constexpr int PIN_TFT_MOSI = 10, PIN_TFT_SCK = 11, PIN_TFT_MISO = 2;
inline constexpr int PIN_TFT_CS = 12, PIN_TFT_DC = 13, PIN_TFT_RST = 14;
inline constexpr int PIN_TOUCH_CS = 8, PIN_TOUCH_IRQ = 9;
inline constexpr int TOUCH_RAW_X_MIN = 240, TOUCH_RAW_X_MAX = 3850;
inline constexpr int TOUCH_RAW_Y_MIN = 240, TOUCH_RAW_Y_MAX = 3850;
inline constexpr int PIN_HC05_STATE = 3, PIN_HC05_RX = 6, PIN_HC05_TX = 7;
inline constexpr uint32_t HC05_BAUD = 9600;
inline constexpr int PIN_LEFT = 15, PIN_RIGHT = 16, PIN_UP = 17, PIN_DOWN = 18;
#if CONFIG_IDF_TARGET_ESP32S2
// GPIO48 is absent on S2; its usual RGB pin conflicts with elevation.
inline constexpr int PIN_RGB = -1;
#elif defined(RGB_BUILTIN)
inline constexpr int PIN_RGB = RGB_BUILTIN;
#else
inline constexpr int PIN_RGB = 48;
#endif
inline constexpr uint8_t RGB_LEVEL = 5;
inline constexpr uint32_t RGB_FLASH_MS = 100, RGB_HEARTBEAT_MS = 2000, RGB_HEARTBEAT_ON_MS = 100;
inline constexpr uint16_t TCP_PORT = 4533;
inline constexpr const char *AP_SSID = "G5500-Rotator";
inline constexpr const char *AP_PASS = "";
} // namespace board
} // namespace forkrot
