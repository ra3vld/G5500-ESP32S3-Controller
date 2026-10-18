// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include "build_config_options.hpp"

// НАСТРОЙКИ СБОРКИ — редактируйте значения справа от #define.
// Флаги -D из Arduino CLI / PlatformIO имеют приоритет над этим файлом.
// Сохраняйте строки #ifndef / #endif: они позволяют переопределять настройки.
// Распиновка — в board_*.hpp; параметры оборудования — в веб-интерфейсе.

// 1. ПЛАТА
// BOARD_AUTO      — по плате, выбранной в Arduino IDE / PlatformIO (рекомендуется).
// BOARD_S3        — ESP32-S3 DevKitC-1 N16R8.
// BOARD_S2_MINI   — LOLIN / WEMOS S2 Mini.
// BOARD_S2_LEGACY — прежняя проводка S2; только DRIVE_YAESU.
// BOARD_ESP32     — ESP32 WROOM / WROVER.
// BOARD_CROWPANEL_21 — Elecrow DHE03921D, круглый ST7701 480x480.
#ifndef BOARD_PROFILE
#define BOARD_PROFILE BOARD_AUTO
#endif

// 2. ДИСПЛЕЙ, BLUETOOTH И WI-FI
// DISPLAY_NONE    — без дисплея.
// DISPLAY_ILI9341 — TFT ILI9341 + сенсор XPT2046.
// DISPLAY_LCD1602 — параллельный LCD1602 / HD44780 (не I2C).
// DISPLAY_CROWPANEL_21 — встроенный экран CrowPanel 2.1 (только эта плата).
#ifndef DISPLAY_KIND
#define DISPLAY_KIND DISPLAY_NONE
#endif

// Остальное оборудование настраивается в вебе: Настройки → Оборудование.
// Начальные значения и совместимость прежних -D — в runtime_defaults.hpp.
#include "runtime_defaults.hpp"

#include "build_config_checks.hpp"
