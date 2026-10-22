// G5500 Fork; GPL-3.0.
#pragma once
// Initial values only. Saved runtime settings take precedence.
// Legacy -D flags remain usable for a fresh device; they never remove drivers.
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
// 1 — имитация до 13,4 градуса/с при 100%: скорость/разгон/торможение как у привода.
// Моторные GPIO/PWM и ADC отключены.
// 0 — физический привод (обычная CrowPanel без профиля модификации блокирует движение).
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

// КНОПКА ПОДТВЕРЖДЕНИЯ ЦЕЛИ
// Свободный GPIO; -1 отключает кнопку. По умолчанию GPIO0 (кнопка BOOT).
#ifndef USER_BUTTON_GPIO
#define USER_BUTTON_GPIO ((SELECTED_BOARD == BOARD_CROWPANEL_21 || SELECTED_BOARD == BOARD_WT32_ETH01) ? -1 : 0)
#endif
// 1 — нажатие замыкает GPIO на GND; 0 — нажатие подаёт HIGH.
#ifndef USER_BUTTON_ACTIVE_LOW
#define USER_BUTTON_ACTIVE_LOW 1
#endif
// 1 — внутренняя подтяжка к питанию; 0 — внешняя подтяжка, например для HIGH.
#ifndef USER_BUTTON_PULLUP
#define USER_BUTTON_PULLUP 1
#endif

// НАЧАЛЬНЫЙ УГОЛ ВЫБЕГА, ГРАДУСЫ (больше 0, не больше 90)
// Веб-настройка «Угол выбега» имеет приоритет; в имитации по умолчанию 0°.
// 5° — начальный порог отключения, не гарантия точности.
// Пересечение цели также завершает движение; обратной коррекции нет.
#ifndef AZ_STOP_TOLERANCE_DEG
#define AZ_STOP_TOLERANCE_DEG 5.0f
#endif
#ifndef EL_STOP_TOLERANCE_DEG
#define EL_STOP_TOLERANCE_DEG 5.0f
#endif
