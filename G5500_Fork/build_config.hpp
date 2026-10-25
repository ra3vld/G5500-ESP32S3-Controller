// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include "build_config_options.hpp"

// Compile-time capabilities. Unlike runtime defaults these remove services.
#ifndef WEB_INTERFACE_ENABLED
#define WEB_INTERFACE_ENABLED 1
#endif
#ifndef BLUETOOTH_SUPPORT
#define BLUETOOTH_SUPPORT 1
#endif

// НАСТРОЙКИ СБОРКИ — редактируйте значения справа от #define.
// Флаги -D из Arduino CLI / PlatformIO имеют приоритет над этим файлом.
// Сохраняйте строки #ifndef / #endif: они позволяют переопределять настройки.
// Начальная распиновка — в board_*.hpp; GPIO и оборудование меняются через веб/API.

// 1. ПЛАТА
// BOARD_AUTO      — по плате, выбранной в Arduino IDE / PlatformIO (рекомендуется).
// BOARD_S3        — ESP32-S3 DevKitC-1 N16R8.
// BOARD_S2_MINI   — LOLIN / WEMOS S2 Mini.
// BOARD_S2_LEGACY — прежняя проводка S2; только DRIVE_YAESU.
// BOARD_ESP32     — ESP32 WROOM / WROVER.
// BOARD_WT32_ETH01 — Wireless-Tag WT32-ETH01, LAN8720 Ethernet, без дисплея.
// BOARD_WAVESHARE_S3_ETH — Waveshare ESP32-S3-ETH, W5500 + DVP-камера.
// BOARD_CROWPANEL_21 — Elecrow DHE03921D, круглый ST7701 480x480.
// BOARD_ESP32_2424S012C — Guition ESP32-C3, GC9A01 240x240 + CST816.
// BOARD_ESP32_C3 — обычная ESP32-C3 с Flash 4 МБ, без дисплея.
// BOARD_ESP32_3248S035C — Sunton ESP32, ST7796 480x320 + GT911.
#ifndef BOARD_PROFILE
#define BOARD_PROFILE BOARD_AUTO
#endif

// DVP-камера Waveshare: JPEG на главной странице. 0 освобождает её GPIO.
#ifndef CAMERA_ENABLED
#define CAMERA_ENABLED (SELECTED_BOARD == BOARD_WAVESHARE_S3_ETH)
#endif

// Только переделанная CrowPanel: GPIO6/P48 — ADC вместо подсветки,
// RX/TX — привод AZ; GPIO0/BOOT — инверсный PWM Yaesu / разрешение BTS7960.
// Обычная CrowPanel должна оставаться с 0. Инструкция: docs/boards/crowpanel-az.md.
#ifndef CROWPANEL_LOCAL_DRIVE
#define CROWPANEL_LOCAL_DRIVE 0
#endif

// Расширенная телеметрия экрана и ввода по native USB; USB-команды отключены.
#ifndef CROWPANEL_USB_TRACE
#define CROWPANEL_USB_TRACE 0
#endif

// 2. ДИСПЛЕЙ, BLUETOOTH И WI-FI
// DISPLAY_NONE    — без дисплея.
// DISPLAY_ILI9341 — TFT ILI9341 + сенсор XPT2046.
// DISPLAY_LCD1602 — параллельный LCD1602 / HD44780 (не I2C).
// DISPLAY_CROWPANEL_21 — встроенный экран CrowPanel 2.1 (только эта плата).
// DISPLAY_GC9A01 — встроенный экран ESP32-2424S012C, включая Y(B).
// DISPLAY_ST7796 — встроенный экран ESP32-3248S035C.
#ifndef DISPLAY_KIND
#define DISPLAY_KIND DISPLAY_NONE
#endif

// Остальное оборудование настраивается в вебе: Настройки → Оборудование.
// Начальные значения и совместимость прежних -D — в runtime_defaults.hpp.
#include "runtime_defaults.hpp"

// 3. ЛОКАЛЬНОЕ УПРАВЛЕНИЕ ОСЯМИ (параметры сборки, не NVS).
// NONE — отключено; BUTTONS — четыре кнопки на GND; JOYSTICK — два ADC1.
// -1 отключает пару кнопок / аналоговый вход соответствующей оси.
// Подключение и примеры: docs/control/local-controls.md.
#ifndef LOCAL_INPUT_MODE
#define LOCAL_INPUT_MODE LOCAL_INPUT_NONE
#endif
#ifndef LOCAL_AZ_NEG_GPIO
#define LOCAL_AZ_NEG_GPIO -1
#endif
#ifndef LOCAL_AZ_POS_GPIO
#define LOCAL_AZ_POS_GPIO -1
#endif
#ifndef LOCAL_EL_NEG_GPIO
#define LOCAL_EL_NEG_GPIO -1
#endif
#ifndef LOCAL_EL_POS_GPIO
#define LOCAL_EL_POS_GPIO -1
#endif
#ifndef LOCAL_AZ_ADC_GPIO
#define LOCAL_AZ_ADC_GPIO -1
#endif
#ifndef LOCAL_EL_ADC_GPIO
#define LOCAL_EL_ADC_GPIO -1
#endif
// Калибровка джойстика: отсчёты analogRead(), 12 бит, 0..4095.
#ifndef LOCAL_AZ_ADC_MIN
#define LOCAL_AZ_ADC_MIN 0
#endif
#ifndef LOCAL_AZ_ADC_CENTER
#define LOCAL_AZ_ADC_CENTER 2048
#endif
#ifndef LOCAL_AZ_ADC_MAX
#define LOCAL_AZ_ADC_MAX 4095
#endif
#ifndef LOCAL_EL_ADC_MIN
#define LOCAL_EL_ADC_MIN 0
#endif
#ifndef LOCAL_EL_ADC_CENTER
#define LOCAL_EL_ADC_CENTER 2048
#endif
#ifndef LOCAL_EL_ADC_MAX
#define LOCAL_EL_ADC_MAX 4095
#endif
#ifndef LOCAL_AZ_INVERT
#define LOCAL_AZ_INVERT 0
#endif
#ifndef LOCAL_EL_INVERT
#define LOCAL_EL_INVERT 0
#endif
#ifndef LOCAL_ADC_DEADZONE
#define LOCAL_ADC_DEADZONE 200
#endif
#ifndef LOCAL_ADC_HYSTERESIS
#define LOCAL_ADC_HYSTERESIS 50
#endif

#include "build_config_checks.hpp"

// One graphics stack; drivers and buffer policy remain board-specific.
#define GRAPHICAL_DISPLAY (DISPLAY_KIND == DISPLAY_ILI9341 || DISPLAY_KIND == DISPLAY_CROWPANEL_21 || DISPLAY_KIND == DISPLAY_GC9A01 || DISPLAY_KIND == DISPLAY_ST7796)

// SPI profiles have a 240-pixel short side; omit unused large font bitmaps.
#define PANEL_COMPACT_FONTS (DISPLAY_KIND == DISPLAY_ILI9341 || DISPLAY_KIND == DISPLAY_GC9A01 || DISPLAY_KIND == DISPLAY_ST7796)
