// Persisted pin roles. Table order is part of hardware record version 2. GPL-3.0.
#pragma once
#include "board_defaults.hpp"
#include <array>
#include <cstdint>
#include <string_view>
namespace forkrot {
namespace board {
inline constexpr int PIN_LOCAL_AZ_NEG_GPIO = LOCAL_AZ_NEG_GPIO;
inline constexpr int PIN_LOCAL_AZ_POS_GPIO = LOCAL_AZ_POS_GPIO;
inline constexpr int PIN_LOCAL_EL_NEG_GPIO = LOCAL_EL_NEG_GPIO;
inline constexpr int PIN_LOCAL_EL_POS_GPIO = LOCAL_EL_POS_GPIO;
inline constexpr int PIN_LOCAL_AZ_ADC_GPIO = LOCAL_AZ_ADC_GPIO;
inline constexpr int PIN_LOCAL_EL_ADC_GPIO = LOCAL_EL_ADC_GPIO;
}

enum class PinKind : uint8_t { Input, Output, Adc };
enum class PinGroup : uint8_t { Drive, Uart, Hc05, Indicator, Tft, Lcd, Buttons, Joystick };
#define FORK_PIN_MAP(X) \
 X(azAdc, PIN_AZ_ADC, Adc, Drive, false) \
 X(elAdc, PIN_EL_ADC, Adc, Drive, false) \
 X(azNegative, PIN_LEFT, Output, Drive, false) \
 X(azPositive, PIN_RIGHT, Output, Drive, false) \
 X(elNegative, PIN_DOWN, Output, Drive, false) \
 X(elPositive, PIN_UP, Output, Drive, false) \
 X(azSpeedEnable, PIN_SPEED, Output, Drive, false) \
 X(elSpeedEnable, PIN_EL_SPEED, Output, Drive, false) \
 X(uartRx, PIN_UART_RX, Input, Uart, true) \
 X(uartTx, PIN_UART_TX, Output, Uart, true) \
 X(hc05Rx, PIN_HC05_RX, Input, Hc05, false) \
 X(hc05Tx, PIN_HC05_TX, Output, Hc05, false) \
 X(hc05State, PIN_HC05_STATE, Input, Hc05, false) \
 X(rgb, PIN_RGB, Output, Indicator, true) \
 X(led, PIN_LED, Output, Indicator, true) \
 X(tftMosi, PIN_TFT_MOSI, Output, Tft, false) \
 X(tftSck, PIN_TFT_SCK, Output, Tft, false) \
 X(tftMiso, PIN_TFT_MISO, Input, Tft, false) \
 X(tftCs, PIN_TFT_CS, Output, Tft, false) \
 X(tftDc, PIN_TFT_DC, Output, Tft, false) \
 X(tftRst, PIN_TFT_RST, Output, Tft, true) \
 X(touchCs, PIN_TOUCH_CS, Output, Tft, false) \
 X(touchIrq, PIN_TOUCH_IRQ, Input, Tft, true) \
 X(lcdRs, PIN_LCD_RS, Output, Lcd, false) \
 X(lcdE, PIN_LCD_E, Output, Lcd, false) \
 X(lcdD4, PIN_LCD_D4, Output, Lcd, false) \
 X(lcdD5, PIN_LCD_D5, Output, Lcd, false) \
 X(lcdD6, PIN_LCD_D6, Output, Lcd, false) \
 X(lcdD7, PIN_LCD_D7, Output, Lcd, false) \
 X(localAzNegative, PIN_LOCAL_AZ_NEG_GPIO, Input, Buttons, true) \
 X(localAzPositive, PIN_LOCAL_AZ_POS_GPIO, Input, Buttons, true) \
 X(localElNegative, PIN_LOCAL_EL_NEG_GPIO, Input, Buttons, true) \
 X(localElPositive, PIN_LOCAL_EL_POS_GPIO, Input, Buttons, true) \
 X(localAzAdc, PIN_LOCAL_AZ_ADC_GPIO, Adc, Joystick, true) \
 X(localElAdc, PIN_LOCAL_EL_ADC_GPIO, Adc, Joystick, true)
struct PinMap {
#define FORK_PIN_MEMBER(name, initial, kind, group, optional) int8_t name = board::initial < 0 ? -1 : board::initial;
  FORK_PIN_MAP(FORK_PIN_MEMBER)
#undef FORK_PIN_MEMBER
  PinMap() {
    if (SELECTED_BOARD == BOARD_CROWPANEL_21 && BLUETOOTH_BACKEND == BT_HC05) uartRx=uartTx=-1;
  }
  bool operator==(const PinMap &) const;
  bool singleAxis() const { return elAdc<0 && elNegative<0 && elPositive<0 && elSpeedEnable<0; }
};
struct PinDefinition {
  const char *name, *field;
  int8_t PinMap::*member;
  PinKind kind;
  PinGroup group;
  bool optional;
};
inline constexpr std::array<PinDefinition,35> pinDefinitions{{
#define FORK_PIN_DEFINITION(name, initial, kind, group, optional) {#name,"pin." #name,&PinMap::name,PinKind::kind,PinGroup::group,optional},
  FORK_PIN_MAP(FORK_PIN_DEFINITION)
#undef FORK_PIN_DEFINITION
}};
#undef FORK_PIN_MAP
inline bool PinMap::operator==(const PinMap &other) const {
  for (const auto &d:pinDefinitions) if (this->*d.member != other.*d.member) return false;
  return true;
}
inline const PinDefinition *pinDefinition(std::string_view field) {
  for (const auto &d:pinDefinitions) if (field==d.field) return &d;
  return nullptr;
}
inline const char *pinGroupName(PinGroup group) {
  switch(group) {
  case PinGroup::Drive:return "drive"; case PinGroup::Uart:return "uart";
  case PinGroup::Hc05:return "hc05"; case PinGroup::Indicator:return "indicator";
  case PinGroup::Buttons:return "buttons"; case PinGroup::Joystick:return "joystick";
  case PinGroup::Tft:return "tft"; case PinGroup::Lcd:return "lcd";
  }
  return "";
}
inline const char *pinKindName(PinKind kind) { return kind==PinKind::Adc?"adc":kind==PinKind::Output?"output":"input"; }
}
