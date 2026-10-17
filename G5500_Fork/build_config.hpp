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

// 1 — включить выбранный Bluetooth; 0 — выключить.
#ifndef BLUETOOTH_ENABLED
#define BLUETOOTH_ENABLED 0
#endif

// BT_AUTO — встроенный SPP на ESP32, внешний HC-05 на S2/S3.
// BT_HC05 — внешний модуль по UART на любой плате, включая ESP32.
// BT_ESP32_SPP — встроенный Bluetooth Classic, только обычная ESP32.
#ifndef BLUETOOTH_MODE
#define BLUETOOTH_MODE BT_AUTO
#endif

// Только обычная ESP32: 0 — AP как резерв, при связи с роутером только STA;
// 1 — принудительно сохранять AP вместе со STA. На S2/S3 AP включена всегда.
// ВНИМАНИЕ: 1 увеличивает расход RAM и может вызывать зависания, перезагрузки
// и отказы Wi-Fi/веба, особенно вместе со встроенным Bluetooth SPP.
#ifndef ESP32_FORCE_AP_STA
#define ESP32_FORCE_AP_STA 0
#endif

// 3. УПРАВЛЕНИЕ ДВИГАТЕЛЯМИ
// 1 — имитация 13,4 градуса/с: моторные GPIO/PWM и ADC отключены.
// 0 — физический привод (CrowPanel без адаптера блокирует движение).
// По умолчанию имитация включена только на CrowPanel; можно заменить на 0 или 1.
#ifndef DRIVE_SIMULATION
#define DRIVE_SIMULATION (SELECTED_BOARD == BOARD_CROWPANEL_21)
#endif

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
