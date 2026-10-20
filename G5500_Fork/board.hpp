// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include <cstdint>
#include "build_config.hpp"
#include "hardware_config.hpp"
#ifdef ARDUINO
#include <soc/soc_caps.h>
#if !CONFIG_IDF_TARGET_ESP32 && !CONFIG_IDF_TARGET_ESP32S2 && !CONFIG_IDF_TARGET_ESP32S3
#error "Only ESP32, ESP32-S2 and ESP32-S3 are supported"
#endif
#endif
#if SELECTED_BOARD == BOARD_S3
#include "board_s3.hpp"
#elif SELECTED_BOARD == BOARD_S2_LEGACY
#include "board_s2_legacy.hpp"
#elif SELECTED_BOARD == BOARD_S2_MINI
#include "board_s2_mini.hpp"
#elif SELECTED_BOARD == BOARD_ESP32
#include "board_esp32.hpp"
#elif SELECTED_BOARD == BOARD_CROWPANEL_21
#include "board_crowpanel_21.hpp"
#else
#error "Unknown board profile"
#endif
#if ((CONFIG_IDF_TARGET_ESP32S2 && SELECTED_BOARD != BOARD_S2_LEGACY && SELECTED_BOARD != BOARD_S2_MINI) || (CONFIG_IDF_TARGET_ESP32S3 && SELECTED_BOARD != BOARD_S3 && SELECTED_BOARD != BOARD_CROWPANEL_21) || (CONFIG_IDF_TARGET_ESP32 && SELECTED_BOARD != BOARD_ESP32))
#error "Board profile does not match the chip"
#endif
#if SELECTED_BOARD == BOARD_ESP32 && (ARDUINO_USB_MODE || ARDUINO_USB_CDC_ON_BOOT)
#error "Classic ESP32 uses a UART bridge, not native USB CDC"
#endif
#if ARDUINO_USB_MODE || ARDUINO_USB_CDC_ON_BOOT
#define FORK_NATIVE_USB 1
#else
#define FORK_NATIVE_USB 0
#endif
#if SELECTED_BOARD == BOARD_ESP32
#define FORK_CHIP_NAME "ESP32"
#define FORK_USB_NAME "UART bridge"
#elif SELECTED_BOARD != BOARD_S3 && SELECTED_BOARD != BOARD_CROWPANEL_21
#define FORK_CHIP_NAME "ESP32-S2"
#define FORK_USB_NAME "CDC"
#else
#define FORK_CHIP_NAME "ESP32-S3"
#define FORK_USB_NAME "Serial/JTAG"
#endif
namespace forkrot { namespace board {
inline constexpr bool WIFI_AP_ALWAYS_ON = SELECTED_BOARD != BOARD_ESP32 || ESP32_FORCE_AP_STA;
inline constexpr int PIN_CONFIRM = USER_BUTTON_GPIO;
inline constexpr int TOUCH_RAW_X_MIN = 240, TOUCH_RAW_X_MAX = 3850;
inline constexpr int TOUCH_RAW_Y_MIN = 240, TOUCH_RAW_Y_MAX = 3850;
inline constexpr uint32_t HC05_BAUD = 9600;
inline constexpr const char *BLUETOOTH_NAME = "G5500-Rotator";
inline constexpr const char *BLUETOOTH_BACKEND_NAME = BLUETOOTH_BACKEND == BT_ESP32_SPP ? "ESP32_SPP" : BLUETOOTH_BACKEND == BT_HC05 ? "HC05" : "NONE";
inline constexpr uint8_t RGB_LEVEL = 5;
inline constexpr uint32_t RGB_FLASH_MS = 100, RGB_HEARTBEAT_MS = 2000, RGB_HEARTBEAT_ON_MS = 100;
inline constexpr uint16_t TCP_PORT = 4533;
inline constexpr const char *AP_SSID = "G5500-Rotator", *AP_PASS = "";
// Exclude flash/PSRAM and native USB pins. S2 Mini additionally requires a header pin.
constexpr bool gpio(int n) {
  if (n < 0) return false;
  if (SELECTED_BOARD == BOARD_ESP32) return n <= 39 && !(n >= 6 && n <= 11) && n != 16 && n != 17 && n != 20 && n != 24 && !(n >= 28 && n <= 31) && n != 37 && n != 38;
  if (SELECTED_BOARD == BOARD_S3 || SELECTED_BOARD == BOARD_CROWPANEL_21) return n <= 48 && n != 22 && n != 23 && n != 24 && n != 25 && !(n >= 26 && n <= 37) && n != 19 && n != 20;
  if (SELECTED_BOARD == BOARD_S2_MINI) return (n <= 18 || n == 21 || (n >= 33 && n <= 40));
  return n <= 46 && !(n >= 22 && n <= 32) && n != 19 && n != 20;
}
constexpr bool output(int n) { return gpio(n) && (SELECTED_BOARD == BOARD_ESP32 ? n < 34 : SELECTED_BOARD == BOARD_CROWPANEL_21 || n != 46); }
constexpr bool pullup(int n) { return gpio(n) && (SELECTED_BOARD == BOARD_ESP32 ? n < 34 : n != 46); }
constexpr bool adc(int n) { return gpio(n) && (SELECTED_BOARD == BOARD_ESP32 ? n >= 32 : n >= 1 && n <= 10); }
inline constexpr int localInputPins[] = {
#if LOCAL_INPUT_MODE == LOCAL_INPUT_BUTTONS
 LOCAL_AZ_NEG_GPIO, LOCAL_AZ_POS_GPIO, LOCAL_EL_NEG_GPIO, LOCAL_EL_POS_GPIO,
#elif LOCAL_INPUT_MODE == LOCAL_INPUT_JOYSTICK
 LOCAL_AZ_ADC_GPIO, LOCAL_EL_ADC_GPIO,
#endif
 -1
};
constexpr bool validLocalInputs() {
 for (int pin : localInputPins) {
  if (pin < -1) return false;
  if (pin == -1) continue;
  if (LOCAL_INPUT_MODE == LOCAL_INPUT_BUTTONS ? !pullup(pin) : !adc(pin)) return false;
 }
 return true;
}
static_assert(validLocalInputs(), "Local controls require pullup-capable GPIO / ADC1 inputs");
// One owner per active wire; shared TFT/touch SPI wires appear only once here.
inline constexpr int activePins[] = {
 PIN_AZ_ADC, PIN_EL_ADC, PIN_LEFT, PIN_RIGHT, PIN_DOWN, PIN_UP, PIN_SPEED,
#if DRIVE_INTERFACE != DRIVE_YAESU
 PIN_EL_SPEED,
#endif
 (SELECTED_BOARD == BOARD_CROWPANEL_21 && BLUETOOTH_BACKEND == BT_HC05) ? -1 : PIN_UART_RX,
 (SELECTED_BOARD == BOARD_CROWPANEL_21 && BLUETOOTH_BACKEND == BT_HC05) ? -1 : PIN_UART_TX, PIN_CONFIRM, PIN_RGB, PIN_LED,
#if LOCAL_INPUT_MODE == LOCAL_INPUT_BUTTONS
 LOCAL_AZ_NEG_GPIO, LOCAL_AZ_POS_GPIO, LOCAL_EL_NEG_GPIO, LOCAL_EL_POS_GPIO,
#elif LOCAL_INPUT_MODE == LOCAL_INPUT_JOYSTICK
 LOCAL_AZ_ADC_GPIO, LOCAL_EL_ADC_GPIO,
#endif
#if BLUETOOTH_BACKEND == BT_HC05
 PIN_HC05_STATE, PIN_HC05_RX, PIN_HC05_TX,
#endif
#if DISPLAY_KIND == DISPLAY_ILI9341
 PIN_TFT_MOSI, PIN_TFT_SCK, PIN_TFT_MISO, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST, PIN_TOUCH_CS, PIN_TOUCH_IRQ,
#elif DISPLAY_KIND == DISPLAY_LCD1602
 PIN_LCD_RS, PIN_LCD_E, PIN_LCD_D4, PIN_LCD_D5, PIN_LCD_D6, PIN_LCD_D7,
#elif DISPLAY_KIND == DISPLAY_CROWPANEL_21
 PIN_I2C_SDA, PIN_I2C_SCL, PIN_ENCODER_A, PIN_ENCODER_B, PIN_BACKLIGHT,
 16, 2, 1, 40, 7, 15, 41, 46, 3, 8, 18, 17, 14, 13, 12, 11, 10, 9, 5, 45, 48, 47, 21,
#endif
};
constexpr bool uniquePins() {
 for (unsigned i=0; i<sizeof(activePins)/sizeof(int); ++i)
  for (unsigned j=i+1; j<sizeof(activePins)/sizeof(int); ++j)
   if (activePins[i] >= 0 && activePins[i] == activePins[j]) return false;
 return true;
}
constexpr bool validPins() {
 for (int n : activePins) if (n >= 0 && !gpio(n)) return false;
 return true;
}
inline constexpr int outputPins[] = {
 PIN_LEFT, PIN_RIGHT, PIN_DOWN, PIN_UP, PIN_SPEED,
 (SELECTED_BOARD == BOARD_CROWPANEL_21 && BLUETOOTH_BACKEND == BT_HC05) ? -1 : PIN_UART_TX, PIN_RGB, PIN_LED,
#if DRIVE_INTERFACE != DRIVE_YAESU
 PIN_EL_SPEED,
#endif
#if BLUETOOTH_BACKEND == BT_HC05
 PIN_HC05_TX,
#endif
#if DISPLAY_KIND == DISPLAY_ILI9341
 PIN_TFT_MOSI, PIN_TFT_SCK, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST, PIN_TOUCH_CS,
#elif DISPLAY_KIND == DISPLAY_LCD1602
 PIN_LCD_RS, PIN_LCD_E, PIN_LCD_D4, PIN_LCD_D5, PIN_LCD_D6, PIN_LCD_D7,
#endif
};
constexpr bool validOutputs() { for (int n : outputPins) if (n >= 0 && !output(n)) return false; return true; }
static_assert(validOutputs(), "Input-only GPIO assigned to an output");
static_assert(validPins(), "Unavailable/reserved GPIO in active board configuration");
static_assert(uniquePins(), "Conflicting active peripheral GPIOs");
static_assert(PIN_CONFIRM == -1 || (gpio(PIN_CONFIRM) && (!USER_BUTTON_PULLUP || pullup(PIN_CONFIRM))), "Invalid user button GPIO/pullup");
#if CROWPANEL_LOCAL_DRIVE
static_assert(adc(PIN_AZ_ADC) && output(PIN_LEFT) && output(PIN_RIGHT) && output(PIN_SPEED), "CrowPanel local AZ requires ADC1 and three native outputs");
#endif
#if SELECTED_BOARD != BOARD_CROWPANEL_21
static_assert(adc(PIN_AZ_ADC) && adc(PIN_EL_ADC), "Feedback must use ADC1 GPIO");
static_assert(output(PIN_LEFT) && output(PIN_RIGHT) && output(PIN_UP) && output(PIN_DOWN) && output(PIN_SPEED) && output(PIN_UART_TX), "Invalid output GPIO");
#endif
#if DRIVE_INTERFACE != DRIVE_YAESU && !CROWPANEL_LOCAL_DRIVE
static_assert(output(PIN_EL_SPEED), "Independent drive requires two speed/enable outputs");
#endif
#if defined(SOC_LEDC_CHANNEL_NUM)
static_assert(SOC_LEDC_CHANNEL_NUM >= (CROWPANEL_LOCAL_DRIVE ? 2 : DRIVE_INTERFACE == DRIVE_BTS7960 ? 4 : DRIVE_INTERFACE == DRIVE_SEPARATE_PWM ? 2 : 1), "Insufficient LEDC channels");
#endif
// Runtime ownership validation uses the same physical board/display map. Motor
// and Bluetooth selections no longer remove code or make unsafe pins available.
inline const char *validateHardware(const HardwareConfig &c, bool hudUart = false) {
  if (!BLUETOOTH_SUPPORT && c.bluetooth) return "bluetooth_not_supported";
  if (c.drive > DRIVE_BTS7960 || c.bluetoothMode < BT_HC05 || c.bluetoothMode > BT_AUTO ||
      (c.pwmInverted && c.drive != DRIVE_BTS7960)) return "invalid_hardware";
  if (c.backend() == BT_ESP32_SPP && SELECTED_BOARD != BOARD_ESP32) return "spp_not_supported";
  if (SELECTED_BOARD == BOARD_CROWPANEL_21 && (c.buttonGpio != -1 ||
      (CROWPANEL_LOCAL_DRIVE ? c.drive == DRIVE_SEPARATE_PWM : c.drive != DRIVE_YAESU)))
    return "crowpanel_hardware_fixed";
  if (CROWPANEL_LOCAL_DRIVE && c.bluetooth) return "crowpanel_uart_is_drive";
  if (CROWPANEL_LOCAL_DRIVE && hudUart) return "crowpanel_uart_is_drive";
  if (!CROWPANEL_LOCAL_DRIVE && c.drive != DRIVE_YAESU && !output(PIN_EL_SPEED)) return "drive_not_supported";
#if defined(SOC_LEDC_CHANNEL_NUM)
  if (SOC_LEDC_CHANNEL_NUM < (CROWPANEL_LOCAL_DRIVE ? 2 : c.drive == DRIVE_BTS7960 ? 4 : c.drive == DRIVE_SEPARATE_PWM ? 2 : 1))
    return "drive_not_supported";
#endif
  if (c.buttonGpio < -1 || (c.buttonGpio >= 0 && (!gpio(c.buttonGpio) ||
      (c.buttonPullup && !pullup(c.buttonGpio))))) return "invalid_button_gpio";
  if (hudUart && c.backend() == BT_HC05 && SELECTED_BOARD == BOARD_CROWPANEL_21) return "uart_in_use_by_hc05";
  int pins[64]; unsigned count = 0;
  auto add = [&](int pin) {
    if (pin < 0) return true;
    if (!gpio(pin)) return false;
    for (unsigned i=0; i<count; ++i) if (pins[i] == pin) return false;
    pins[count++] = pin; return true;
  };
  for (int pin : localInputPins) if (!add(pin)) return "gpio_conflict";
  if (!c.simulation && (SELECTED_BOARD != BOARD_CROWPANEL_21 || CROWPANEL_LOCAL_DRIVE)) {
    for (int pin : {PIN_AZ_ADC, PIN_EL_ADC, PIN_LEFT, PIN_RIGHT, PIN_DOWN, PIN_UP, PIN_SPEED})
      if (!add(pin)) return "gpio_conflict";
    if (c.drive != DRIVE_YAESU && !add(PIN_EL_SPEED)) return "gpio_conflict";
  }
  if (!(SELECTED_BOARD == BOARD_CROWPANEL_21 && c.backend() == BT_HC05))
    if (!add(PIN_UART_RX) || !add(PIN_UART_TX)) return "gpio_conflict";
  for (int pin : {int(c.buttonGpio), PIN_RGB, PIN_LED}) if (!add(pin)) return "gpio_conflict";
  if (c.backend() == BT_HC05)
    for (int pin : {PIN_HC05_STATE, PIN_HC05_RX, PIN_HC05_TX}) if (!add(pin)) return "gpio_conflict";
#if DISPLAY_KIND == DISPLAY_ILI9341
  for (int pin : {PIN_TFT_MOSI, PIN_TFT_SCK, PIN_TFT_MISO, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST, PIN_TOUCH_CS, PIN_TOUCH_IRQ})
    if (!add(pin)) return "gpio_conflict";
#elif DISPLAY_KIND == DISPLAY_LCD1602
  for (int pin : {PIN_LCD_RS, PIN_LCD_E, PIN_LCD_D4, PIN_LCD_D5, PIN_LCD_D6, PIN_LCD_D7})
    if (!add(pin)) return "gpio_conflict";
#elif DISPLAY_KIND == DISPLAY_CROWPANEL_21
  for (int pin : {PIN_I2C_SDA, PIN_I2C_SCL, PIN_ENCODER_A, PIN_ENCODER_B, PIN_BACKLIGHT,
       16,2,1,40,7,15,41,46,3,8,18,17,14,13,12,11,10,9,5,45,48,47,21})
    if (!add(pin)) return "gpio_conflict";
#endif
  return nullptr;
}
} }
