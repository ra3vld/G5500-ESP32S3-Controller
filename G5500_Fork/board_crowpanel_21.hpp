// Elecrow DHE03921D. Wiring and LCD sequence ported from uedx_rotator.
#pragma once
namespace forkrot { namespace board {
inline constexpr const char *NAME = "CrowPanel 2.1 DHE03921D";
// No rotator adapter: these identifiers must never reach GPIO/ADC APIs.
inline constexpr int PIN_AZ_ADC = -1, PIN_EL_ADC = -2;
inline constexpr int PIN_LEFT = -1, PIN_RIGHT = -1, PIN_DOWN = -1, PIN_UP = -1;
inline constexpr int PIN_SPEED = -1, PIN_EL_SPEED = -1;
inline constexpr int PIN_RGB = -1, PIN_LED = -1;
// HC-05 optionally takes the external UART; USB diagnostics remain available.
inline constexpr int PIN_UART_RX = BLUETOOTH_ENABLED ? -1 : 44;
inline constexpr int PIN_UART_TX = BLUETOOTH_ENABLED ? -1 : 43;
inline constexpr int PIN_HC05_RX = 44, PIN_HC05_TX = 43, PIN_HC05_STATE = 0;
inline constexpr int PIN_I2C_SDA = 38, PIN_I2C_SCL = 39;
inline constexpr int PIN_ENCODER_A = 42, PIN_ENCODER_B = 4;
inline constexpr int PIN_BACKLIGHT = 6;
} }
