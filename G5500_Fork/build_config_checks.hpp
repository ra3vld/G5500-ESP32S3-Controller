// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once

// Служебная логика. Подключается после настроек из build_config.hpp.
#ifndef TFT_DISPLAY_ENABLED
#define TFT_DISPLAY_ENABLED (DISPLAY_KIND == DISPLAY_ILI9341)
#endif
#if TFT_DISPLAY_ENABLED != (DISPLAY_KIND == DISPLAY_ILI9341)
#error "Conflicting DISPLAY_KIND and TFT_DISPLAY_ENABLED"
#endif

static_assert(BOARD_PROFILE >= BOARD_AUTO && BOARD_PROFILE <= BOARD_ESP32, "Invalid board profile");
#if BOARD_PROFILE == BOARD_AUTO
#if defined(ARDUINO_LOLIN_S2_MINI)
#define SELECTED_BOARD BOARD_S2_MINI
#elif CONFIG_IDF_TARGET_ESP32S2
#define SELECTED_BOARD BOARD_S2_LEGACY
#elif CONFIG_IDF_TARGET_ESP32
#define SELECTED_BOARD BOARD_ESP32
#else
#define SELECTED_BOARD BOARD_S3
#endif
#else
#define SELECTED_BOARD BOARD_PROFILE
#endif

#if !BLUETOOTH_ENABLED
#define BLUETOOTH_BACKEND BT_NONE
#elif SELECTED_BOARD == BOARD_ESP32
#define BLUETOOTH_BACKEND BT_ESP32_SPP
#else
#define BLUETOOTH_BACKEND BT_HC05
#endif

#if DISPLAY_KIND < 0 || DISPLAY_KIND > 2 || DRIVE_INTERFACE < 0 || DRIVE_INTERFACE > 2
#error "Unknown display or drive interface"
#endif
#if BTS7960_PWM_INVERTED != 0 && BTS7960_PWM_INVERTED != 1
#error "BTS7960_PWM_INVERTED must be 0 or 1"
#endif
#if BTS7960_PWM_INVERTED && DRIVE_INTERFACE != DRIVE_BTS7960
#error "PWM inversion requires DRIVE_BTS7960"
#endif
#if (BLUETOOTH_ENABLED != 0 && BLUETOOTH_ENABLED != 1) || (USER_BUTTON_ACTIVE_LOW != 0 && USER_BUTTON_ACTIVE_LOW != 1) || (USER_BUTTON_PULLUP != 0 && USER_BUTTON_PULLUP != 1)
#error "Boolean build flags must be 0 or 1"
#endif
static_assert(AZ_STOP_TOLERANCE_DEG > 0 && AZ_STOP_TOLERANCE_DEG <= 90, "Invalid AZ tolerance");
static_assert(EL_STOP_TOLERANCE_DEG > 0 && EL_STOP_TOLERANCE_DEG <= 90, "Invalid EL tolerance");
