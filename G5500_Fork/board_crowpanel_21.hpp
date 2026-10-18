// Elecrow DHE03921D. Wiring and LCD sequence ported from uedx_rotator.
#pragma once
namespace forkrot { namespace board {
inline constexpr const char *NAME = CROWPANEL_LOCAL_DRIVE ? "CrowPanel 2.1 AZ" : "CrowPanel 2.1 DHE03921D";
// Unmodified profile has no adapter. Local AZ requires the documented PCB modification.
inline constexpr int PIN_AZ_ADC = CROWPANEL_LOCAL_DRIVE ? 6 : -1, PIN_EL_ADC = -2;
inline constexpr int PIN_LEFT = CROWPANEL_LOCAL_DRIVE ? 44 : -1, PIN_RIGHT = CROWPANEL_LOCAL_DRIVE ? 43 : -1;
inline constexpr int PIN_DOWN = -1, PIN_UP = -1;
// BOOT: inverted speed PWM / active-LOW bridge enable, open drain, pull-up 3.3 V.
inline constexpr int PIN_SPEED = CROWPANEL_LOCAL_DRIVE ? 0 : -1, PIN_EL_SPEED = -1;
inline constexpr int PIN_RGB = -1, PIN_LED = -1;
// HC-05 optionally takes the external UART; USB diagnostics remain available.
inline constexpr int PIN_UART_RX = CROWPANEL_LOCAL_DRIVE ? -1 : 44;
inline constexpr int PIN_UART_TX = CROWPANEL_LOCAL_DRIVE ? -1 : 43;
inline constexpr int PIN_HC05_RX = 44, PIN_HC05_TX = 43, PIN_HC05_STATE = 0;
inline constexpr int PIN_I2C_SDA = 38, PIN_I2C_SCL = 39;
inline constexpr int PIN_ENCODER_A = 42, PIN_ENCODER_B = 4;
inline constexpr int PIN_BACKLIGHT = CROWPANEL_LOCAL_DRIVE ? -1 : 6;
} }
