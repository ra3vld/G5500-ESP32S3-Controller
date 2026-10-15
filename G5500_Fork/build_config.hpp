// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include "build_config_options.hpp"

// НАСТРОЙКИ СБОРКИ — редактируйте значения справа от #define.
// Флаги -D из Arduino CLI / PlatformIO имеют приоритет над этим файлом.
// Сохраняйте строки #ifndef / #endif: они позволяют переопределять настройки.
// Распиновка — в board_*.hpp; калибровки и скорость — в веб-интерфейсе.

// 1. ПЛАТА
// BOARD_AUTO      — по плате, выбранной в Arduino IDE / PlatformIO (рекомендуется).
// BOARD_S3        — ESP32-S3 DevKitC-1 N16R8.
// BOARD_S2_MINI   — LOLIN / WEMOS S2 Mini.
// BOARD_S2_LEGACY — прежняя проводка S2; только DRIVE_YAESU.
// BOARD_ESP32     — ESP32 WROOM / WROVER.
#ifndef BOARD_PROFILE
#define BOARD_PROFILE BOARD_AUTO
#endif

// 2. ДИСПЛЕЙ И BLUETOOTH
// DISPLAY_NONE    — без дисплея.
// DISPLAY_ILI9341 — TFT ILI9341 + сенсор XPT2046.
// DISPLAY_LCD1602 — параллельный LCD1602 / HD44780 (не I2C).
#ifndef DISPLAY_KIND
#define DISPLAY_KIND DISPLAY_NONE
#endif

// 1 — включён: встроенный SPP на ESP32, внешний HC-05 на S2/S3; 0 — выключен.
#ifndef BLUETOOTH_ENABLED
#define BLUETOOTH_ENABLED 1
#endif

// 3. УПРАВЛЕНИЕ ДВИГАТЕЛЯМИ
// DRIVE_YAESU        — штатный блок Yaesu; каскад скорости G-800DXA опционален.
// DRIVE_SEPARATE_PWM — два направления + отдельный PWM каждой оси.
// DRIVE_BTS7960      — RPWM / LPWM + ENABLE каждого H-моста.
#ifndef DRIVE_INTERFACE
#define DRIVE_INTERFACE DRIVE_YAESU
#endif

// Только BTS7960: 0 — прямые PWM; 1 — инвертирующие оптроны RPWM / LPWM.
// ENABLE всегда активен HIGH. Для других интерфейсов оставьте 0.
#ifndef BTS7960_PWM_INVERTED
#define BTS7960_PWM_INVERTED 0
#endif

// 4. КНОПКА ПОДТВЕРЖДЕНИЯ ЦЕЛИ
// Свободный GPIO; -1 отключает кнопку. По умолчанию GPIO0 (кнопка BOOT).
#ifndef USER_BUTTON_GPIO
#define USER_BUTTON_GPIO 0
#endif
// 1 — нажатие замыкает GPIO на GND; 0 — нажатие подаёт HIGH.
#ifndef USER_BUTTON_ACTIVE_LOW
#define USER_BUTTON_ACTIVE_LOW 1
#endif
// 1 — внутренняя подтяжка к питанию; 0 — внешняя подтяжка, например для HIGH.
#ifndef USER_BUTTON_PULLUP
#define USER_BUTTON_PULLUP 1
#endif

// 5. ПОРОГ ОСТАНОВКИ, ГРАДУСЫ (больше 0, не больше 90)
// 5° — порог управления при шумной обратной связи, не гарантия точности.
// Пересечение цели также завершает движение; обратной коррекции нет.
#ifndef AZ_STOP_TOLERANCE_DEG
#define AZ_STOP_TOLERANCE_DEG 5.0f
#endif
#ifndef EL_STOP_TOLERANCE_DEG
#define EL_STOP_TOLERANCE_DEG 5.0f
#endif

#include "build_config_checks.hpp"
