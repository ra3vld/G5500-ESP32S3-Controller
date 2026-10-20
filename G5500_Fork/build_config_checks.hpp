// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#if (WEB_INTERFACE_ENABLED != 0 && WEB_INTERFACE_ENABLED != 1) || (BLUETOOTH_SUPPORT != 0 && BLUETOOTH_SUPPORT != 1)
#error "Service capabilities must be 0 or 1"
#endif
#if !BLUETOOTH_SUPPORT && BLUETOOTH_ENABLED
#error "Bluetooth is not included in this build"
#endif

// Служебная логика. Подключается после настроек из build_config.hpp.
#ifndef TFT_DISPLAY_ENABLED
#define TFT_DISPLAY_ENABLED (DISPLAY_KIND == DISPLAY_ILI9341)
#endif
#if TFT_DISPLAY_ENABLED != (DISPLAY_KIND == DISPLAY_ILI9341)
#error "Conflicting DISPLAY_KIND and TFT_DISPLAY_ENABLED"
#endif

static_assert(BOARD_PROFILE >= BOARD_AUTO && BOARD_PROFILE <= BOARD_CROWPANEL_21, "Invalid board profile");
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
#elif BLUETOOTH_MODE != BT_AUTO
#define BLUETOOTH_BACKEND BLUETOOTH_MODE
#elif SELECTED_BOARD == BOARD_ESP32
#define BLUETOOTH_BACKEND BT_ESP32_SPP
#else
#define BLUETOOTH_BACKEND BT_HC05
#endif

#if BLUETOOTH_MODE < BT_HC05 || BLUETOOTH_MODE > BT_AUTO
#error "Invalid BLUETOOTH_MODE: use BT_AUTO, BT_HC05 or BT_ESP32_SPP"
#endif
#if BLUETOOTH_BACKEND == BT_ESP32_SPP && SELECTED_BOARD != BOARD_ESP32
#error "Built-in Bluetooth SPP requires BOARD_ESP32"
#endif
#if DRIVE_SIMULATION != 0 && DRIVE_SIMULATION != 1
#error "DRIVE_SIMULATION must be 0 or 1"
#endif

#if DISPLAY_KIND < 0 || DISPLAY_KIND > 3 || DRIVE_INTERFACE < 0 || DRIVE_INTERFACE > 2
#error "Unknown display or drive interface"
#endif
#if SELECTED_BOARD == BOARD_CROWPANEL_21
#if DISPLAY_KIND != DISPLAY_CROWPANEL_21 || USER_BUTTON_GPIO != -1
#error "CrowPanel requires its RGB display and PCF8574 button (USER_BUTTON_GPIO=-1)"
#endif
#if (!CROWPANEL_LOCAL_DRIVE && DRIVE_INTERFACE != DRIVE_YAESU) || DRIVE_INTERFACE == DRIVE_SEPARATE_PWM
#error "CrowPanel local AZ supports only YAESU/BTS7960; unmodified board requires YAESU placeholder"
#endif
#elif DISPLAY_KIND == DISPLAY_CROWPANEL_21
#error "CrowPanel options require BOARD_CROWPANEL_21"
#endif
#if CROWPANEL_LOCAL_DRIVE != 0 && CROWPANEL_LOCAL_DRIVE != 1
#error "CROWPANEL_LOCAL_DRIVE must be 0 or 1"
#endif
#if CROWPANEL_LOCAL_DRIVE && (SELECTED_BOARD != BOARD_CROWPANEL_21 || BLUETOOTH_ENABLED)
#error "CrowPanel local AZ requires BOARD_CROWPANEL_21 and Bluetooth disabled (UART owns motor pins)"
#endif
#if CROWPANEL_LOCAL_DRIVE && defined(ARDUINO) && (!ARDUINO_USB_MODE || !ARDUINO_USB_CDC_ON_BOOT)
#error "CrowPanel local AZ requires native USB diagnostics; UART0 owns motor pins"
#endif
#if BTS7960_PWM_INVERTED != 0 && BTS7960_PWM_INVERTED != 1
#error "BTS7960_PWM_INVERTED must be 0 or 1"
#endif
#if BTS7960_PWM_INVERTED && DRIVE_INTERFACE != DRIVE_BTS7960
#error "PWM inversion requires DRIVE_BTS7960"
#endif
#if ESP32_FORCE_AP_STA != 0 && ESP32_FORCE_AP_STA != 1
#error "ESP32_FORCE_AP_STA must be 0 or 1"
#endif
#if (BLUETOOTH_ENABLED != 0 && BLUETOOTH_ENABLED != 1) || (USER_BUTTON_ACTIVE_LOW != 0 && USER_BUTTON_ACTIVE_LOW != 1) || (USER_BUTTON_PULLUP != 0 && USER_BUTTON_PULLUP != 1)
#error "Boolean build flags must be 0 or 1"
#endif
static_assert(AZ_STOP_TOLERANCE_DEG > 0 && AZ_STOP_TOLERANCE_DEG <= 90, "Invalid AZ tolerance");
static_assert(EL_STOP_TOLERANCE_DEG > 0 && EL_STOP_TOLERANCE_DEG <= 90, "Invalid EL tolerance");
