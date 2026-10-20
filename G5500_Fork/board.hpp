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
constexpr bool output(int n) { return gpio(n) && (SELECTED_BOARD == BOARD_ESP32 ? n < 34 : (SELECTED_BOARD == BOARD_S3 || SELECTED_BOARD == BOARD_CROWPANEL_21) || n != 46); }
constexpr bool pullup(int n) { return gpio(n) && (SELECTED_BOARD == BOARD_ESP32 ? n < 34 : (SELECTED_BOARD == BOARD_S3 || SELECTED_BOARD == BOARD_CROWPANEL_21) || n != 46); }
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
inline const char *localPinOwner(int pin) {
  if (pin < 0) return nullptr;
#if LOCAL_INPUT_MODE == LOCAL_INPUT_BUTTONS
  if (pin == LOCAL_AZ_NEG_GPIO) return "local.azNegative";
  if (pin == LOCAL_AZ_POS_GPIO) return "local.azPositive";
  if (pin == LOCAL_EL_NEG_GPIO) return "local.elNegative";
  if (pin == LOCAL_EL_POS_GPIO) return "local.elPositive";
#elif LOCAL_INPUT_MODE == LOCAL_INPUT_JOYSTICK
  if (pin == LOCAL_AZ_ADC_GPIO) return "local.azJoystick";
  if (pin == LOCAL_EL_ADC_GPIO) return "local.elJoystick";
#endif
  return nullptr;
}
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
// Physical connections on CrowPanel cannot be freed by software switches.
inline const char *fixedPinOwner(int n) {
#if DISPLAY_KIND == DISPLAY_CROWPANEL_21
  if(n==PIN_I2C_SDA || n==PIN_I2C_SCL) return "crowpanel.i2c";
  if(n==PIN_ENCODER_A || n==PIN_ENCODER_B) return "crowpanel.encoder";
  if(n>=0 && n==PIN_BACKLIGHT) return "crowpanel.backlight";
  for(int pin:{16,2,1,40,7,15,41,46,3,8,18,17,14,13,12,11,10,9,5,45,48,47,21})
    if(n==pin) return "crowpanel.display";
#else
  (void)n;
#endif
  return nullptr;
}
inline const char *unavailablePinOwner(int n) {
  if(gpio(n)) {
    if(const auto *owner=fixedPinOwner(n)) return owner;
    return localPinOwner(n);
  }
  if((SELECTED_BOARD==BOARD_ESP32 && ((n>=6 && n<=11) || n==16 || n==17)) ||
      ((SELECTED_BOARD==BOARD_S3 || SELECTED_BOARD==BOARD_CROWPANEL_21) && n>=26 && n<=37) ||
      ((SELECTED_BOARD==BOARD_S2_LEGACY || SELECTED_BOARD==BOARD_S2_MINI) && n>=22 && n<=32)) return "flash_psram";
  if(SELECTED_BOARD!=BOARD_ESP32 && (n==19 || n==20)) return "usb";
  return "unavailable";
}
struct HardwareValidation {
  const char *error=nullptr, *field="", *owner="";
  int pin=-1;
  explicit operator bool() const { return error!=nullptr; }
};
inline bool pinActive(const HardwareConfig &c,const PinDefinition &d) {
  switch(d.group) {
  case PinGroup::Drive:
    if(c.simulation) return false;
    if(c.pins.singleAxis() && (d.member==&PinMap::elAdc || d.member==&PinMap::elNegative ||
        d.member==&PinMap::elPositive || d.member==&PinMap::elSpeedEnable)) return false;
    return d.member!=&PinMap::elSpeedEnable || c.drive!=DRIVE_YAESU;
  case PinGroup::Uart:return true;
  case PinGroup::Hc05:return c.backend()==BT_HC05;
  case PinGroup::Indicator:return true;
  case PinGroup::Tft:return DISPLAY_KIND==DISPLAY_ILI9341;
  case PinGroup::Lcd:return DISPLAY_KIND==DISPLAY_LCD1602;
  }
  return false;
}
inline HardwareValidation validateHardwareDetailed(const HardwareConfig &c,bool hudUart=false) {
  if(!BLUETOOTH_SUPPORT && c.bluetooth) return {"bluetooth_not_supported","bluetoothEnabled"};
  if(c.drive>DRIVE_BTS7960 || c.bluetoothMode<BT_HC05 || c.bluetoothMode>BT_AUTO ||
      (c.pwmInverted && c.drive!=DRIVE_BTS7960)) return {"invalid_hardware"};
  if(c.backend()==BT_ESP32_SPP && SELECTED_BOARD!=BOARD_ESP32) return {"spp_not_supported","bluetoothMode"};
  // The modified adapter has an active-LOW, open-drain speed/enable stage.
  if(CROWPANEL_LOCAL_DRIVE && !c.simulation && (!c.pins.singleAxis() || c.drive==DRIVE_SEPARATE_PWM))
    return {"drive_not_supported","driveInterface"};
#if defined(SOC_LEDC_CHANNEL_NUM)
  const unsigned channels=CROWPANEL_LOCAL_DRIVE?2:c.drive==DRIVE_BTS7960?(c.pins.singleAxis()?2:4):
      c.drive==DRIVE_SEPARATE_PWM?(c.pins.singleAxis()?1:2):1;
  if(!c.simulation && SOC_LEDC_CHANNEL_NUM<channels) return {"drive_not_supported","driveInterface"};
#endif
  if((c.pins.uartRx<0)!=(c.pins.uartTx<0) || (hudUart && c.pins.uartRx<0))
    return {"incomplete_uart","pin.uartRx"};
  const char *owners[49]{};
  auto add=[&](int pin,const char *field,PinKind kind,bool required,bool needsPullup=false)->HardwareValidation {
    if(pin < -1 || pin>48) return {"invalid_gpio",field,"",pin};
    if(pin<0) return required?HardwareValidation{"missing_pin",field,"",pin}:HardwareValidation{};
    if(!gpio(pin)) return {"invalid_gpio",field,unavailablePinOwner(pin),pin};
    if(kind==PinKind::Adc && !adc(pin)) return {"invalid_adc_gpio",field,"",pin};
    if(kind==PinKind::Output && !output(pin)) return {"input_only_gpio",field,"",pin};
    if(needsPullup && !pullup(pin)) return {"invalid_pullup",field,"",pin};
    if(const auto *owner=fixedPinOwner(pin)) return {"gpio_conflict",field,owner,pin};
    if(const auto *owner=localPinOwner(pin)) return {"gpio_conflict",field,owner,pin};
    if(owners[pin]) return {"gpio_conflict",field,owners[pin],pin};
    owners[pin]=field; return {};
  };
  for(const auto &d:pinDefinitions) {
    const int pin=c.pins.*d.member;
    if(pin < -1 || pin>48) return {"invalid_gpio",d.field,"",pin};
    // Inactive roles do not acquire a wire, but an assigned number must still
    // describe a usable signal; simulation cannot make Flash or built-in wiring available.
    if(pin>=0) {
      if(!gpio(pin)) return {"invalid_gpio",d.field,unavailablePinOwner(pin),pin};
      if(d.kind==PinKind::Adc && !adc(pin)) return {"invalid_adc_gpio",d.field,"",pin};
      if(d.kind==PinKind::Output && !output(pin)) return {"input_only_gpio",d.field,"",pin};
      if(const auto *owner=fixedPinOwner(pin)) return {"gpio_conflict",d.field,owner,pin};
    }
    if(!pinActive(c,d)) continue;
    if(auto error=add(pin,d.field,d.kind,!d.optional)) return error;
  }
  if(auto error=add(c.buttonGpio,"buttonGpio",PinKind::Input,false,c.buttonPullup)) return error;
  return {};
}
inline const char *validateHardware(const HardwareConfig &c,bool hudUart=false) {
  return validateHardwareDetailed(c,hudUart).error;
}
// Recovery may expose a confirmed console only when neither saved wiring map
// assigns its wires to a motor. Never fall back to arbitrary default GPIOs.
inline HardwareConfig recoveryHardware(const HardwareRecord &r) {
  auto c=r.good; c.bluetooth=false; c.forceAp=false; c.buttonGpio=-1;
  c.pins.rgb=c.pins.led=-1;
  bool uart=r.hasConfirmed && gpio(c.pins.uartRx) && output(c.pins.uartTx);
  for(const auto *map:{&r.good.pins,&r.candidate.pins})
    for(size_t i=0;i<8;++i) {
      const int pin=map->*pinDefinitions[i].member;
      if(pin>=0 && (pin==c.pins.uartRx || pin==c.pins.uartTx)) uart=false;
    }
  if(!uart) c.pins.uartRx=c.pins.uartTx=-1;
  return c;
}
} }
