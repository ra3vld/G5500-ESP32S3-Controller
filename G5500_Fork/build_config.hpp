// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include "build_config_options.hpp"

// Замеры 2026-10-27: Core 3.3.11, GCC 14.2.0, platform 55.3.311.
// ST = esp32_3248s035c (LTO), S3 = esp32s3_n16r8 (N16R8, DISPLAY_NONE).
// Экономия при 0 в байтах: полный firmware.bin / статическая DRAM / IRAM.
// Пары одного снимка; отдельные результаты не складываются из-за общих зависимостей.

// Compile-time capabilities. Unlike runtime defaults these remove services.
// При 0, Б (BIN/DRAM/IRAM): ST 190464/8048/424; S3 512560/8040/256.
// Вместе с WebSocket; heap HTTP/SDK не измерена. WS: stack 6144 Б, Clients 160 Б, 2376 Б/клиент, 3092 Б/снимок (до 4).
#ifndef WEB_INTERFACE_ENABLED
#define WEB_INTERFACE_ENABLED 1
#endif
// При 0, Б (BIN/DRAM/IRAM): ST 710400/14924/34676; S3 2096/8/0.
// Heap при использовании: SppState 2320 Б или HcState 600 Б; Bluetooth/UART SDK дополнительно, runtime не измерен.
#ifndef BLUETOOTH_SUPPORT
#define BLUETOOTH_SUPPORT 1
#endif

// Возможности сборки: 0 удаляет сервис и его буферы, 1 сохраняет поддержку.
// Экономия нескольких флагов не складывается: часть библиотек общая.
// Heap зависит от подключений; размер буфера не равен полному расходу сервиса.
// При 0, Б (BIN/DRAM/IRAM): ST 333856/12112/232; S3 0/0/0.
// LCD1602 2592/72/0; GC9A01 346288/12048/0; CrowPanel 448224/4920/1536.
// ILI9341: ESP32 350432/15256/612; S3 352576/15304/256.
// Heap LVGL/widget/cache не измерена; ST stripe 7680 Б уже учтена в DRAM; CrowPanel: 921600 Б PSRAM + 38400 Б DMA buffers при старте.
#ifndef DISPLAY_SUPPORT
#define DISPLAY_SUPPORT 1
#endif
// При 0, Б (BIN/DRAM/IRAM): ST 2880/152/0; S3 3712/152/0.
// Отдельных heap-буферов ввода нет; runtime heap драйверов ADC/GPIO не измерена.
#ifndef LOCAL_INPUT_SUPPORT
#define LOCAL_INPUT_SUPPORT 1
#endif
// При 0, Б (BIN/DRAM/IRAM): ST 816688/26198/5428; S3 1169520/30020/8704.
// Без Ethernet также отключаются веб, MQTT, HUD UDP, ArduinoOTA, RSSI/NTP; heap Wi-Fi/lwIP не измерена.
#ifndef WIFI_SUPPORT
#define WIFI_SUPPORT 1
#endif
// При 0, Б (BIN/DRAM/IRAM): ST 0/0/0; S3 0/0/0.
// WT32-ETH01 54528/168/1064; Waveshare S3-ETH 90928/236/1536.
// Heap PHY/W5500/сетевого стека при запуске не измерена; на платах без Ethernet выигрыш 0.
#ifndef ETHERNET_SUPPORT
#define ETHERNET_SUPPORT (SELECTED_BOARD == BOARD_WT32_ETH01 || SELECTED_BOARD == BOARD_WAVESHARE_S3_ETH)
#endif
// При 0, Б (BIN/DRAM/IRAM): ST 44480/280/424; S3 45104/280/256.
// HTTP остаётся. Heap при WS: stack 6144 Б + Clients 160 Б + 2376 Б/клиент + 3092 Б/снимок; полный runtime не измерен.
#ifndef WEB_LIVE_SUPPORT
#define WEB_LIVE_SUPPORT WEB_INTERFACE_ENABLED
#endif
// Несжатая копия HTML для клиентов без gzip; JSON API не зависит от флага.
// При 0, Б (BIN/DRAM/IRAM): ST 0/0/0; S3 317584/0/0.
// Только Flash: браузеру нужен gzip, JSON API сохранён; отдельной heap нет, полный runtime не измерен.
#ifndef WEB_PLAIN_HTML_SUPPORT
#define WEB_PLAIN_HTML_SUPPORT (WEB_INTERFACE_ENABLED && (FORK_S3 || (FORK_CLASSIC_ESP32 && SELECTED_BOARD != BOARD_ESP32_3248S035C)))
#endif
// При 0, Б (BIN/DRAM/IRAM): ST 708592/14916/34676; S3 0/0/0.
// HC-05 остаётся; SppState 2320 Б heap при включении + SDK BT, полный runtime не измерен; на S2/S3/C3 SPP уже отсутствует.
#ifndef BLUETOOTH_SPP_SUPPORT
#define BLUETOOTH_SPP_SUPPORT (BLUETOOTH_SUPPORT && FORK_CLASSIC_ESP32)
#endif
// При 0, Б (BIN/DRAM/IRAM): ST 3696/0/0; S3 3584/0/0.
// Постоянных собственных heap-буферов нет; временные String/ответы и runtime heap не измерены.
#ifndef GS232_SUPPORT
#define GS232_SUPPORT 1
#endif
// При 0, Б (BIN/DRAM/IRAM): ST 4640/1032/0; S3 3968/1032/0.
// Исключается TCP-приёмник; heap TCP/SDK при подключении не измерена.
#ifndef ROTCTLD_SUPPORT
#define ROTCTLD_SUPPORT 1
#endif
// При 0, Б (BIN/DRAM/IRAM): ST 848/0/0; S3 1616/0/0.
// Общий RX 4096 Б статически остаётся, пока включён PstRotator; heap UDP/SDK не измерена.
#ifndef N1MM_SUPPORT
#define N1MM_SUPPORT 1
#endif
// При 0, Б (BIN/DRAM/IRAM): ST 2160/0/0; S3 2176/0/0.
// Общий RX 4096 Б статически остаётся, пока включён N1MM; heap UDP/SDK не измерена.
#ifndef PSTROTATOR_SUPPORT
#define PSTROTATOR_SUPPORT 1
#endif
// При 0, Б (BIN/DRAM/IRAM): ST 7296/2264/0; S3 7264/2264/0.
// CommandServer 8064 Б heap при первом использовании; полный runtime heap не измерен. Сохраните веб/сеть для настройки.
#ifndef COMMAND_API_SUPPORT
#define COMMAND_API_SUPPORT 1
#endif
// При 0, Б (BIN/DRAM/IRAM): ST 40960/2912/0; S3 39360/2904/0.
// Каталог: 920 Б heap/запись; UART transport 6296 Б либо RS485Link 6424 Б при включении; полный runtime не измерен.
#ifndef HUD_SUPPORT
#define HUD_SUPPORT 1
#endif
// При 0, Б (BIN/DRAM/IRAM): ST 528/0/0; S3 496/0/0.
// RX-буфер общий с UART; heap UDP/SDK при включении не измерена. Каталог и другие HUD-транспорты остаются.
#ifndef HUD_UDP_SUPPORT
#define HUD_UDP_SUPPORT (HUD_SUPPORT && (WIFI_SUPPORT || ETHERNET_SUPPORT))
#endif
// При 0, Б (BIN/DRAM/IRAM): ST 5232/0/0; S3 4864/0/0.
// HUD transport: UART 6296 Б либо RS485Link 6424 Б heap при включении, плюс драйверные буферы; runtime не измерен.
#ifndef HUD_UART_SUPPORT
#define HUD_UART_SUPPORT HUD_SUPPORT
#endif
// При 0, Б (BIN/DRAM/IRAM): ST 19472/0/0; S3 18800/0/0.
// Также выключает Modbus: RS485Link 6424 Б, reply 2064 Б, ModbusLink 3196 Б и Mailbox 5456 Б/узел по использованию; runtime не измерен.
#ifndef RS485_SUPPORT
#define RS485_SUPPORT 1
#endif
// При 0, Б (BIN/DRAM/IRAM): ST 14560/0/0; S3 13872/0/0.
// ModbusLink 3196 Б heap при включении + Mailbox 5456 Б/узел при обращении; драйверные расходы/runtime не измерены.
#ifndef MODBUS_RTU_SUPPORT
#define MODBUS_RTU_SUPPORT RS485_SUPPORT
#endif
// При 0, Б (BIN/DRAM/IRAM): ST 27616/1952/0; S3 30544/1944/0.
// Также выключает ArduinoOTA; heap Update/загрузки/SDK при обновлении не измерена.
#ifndef OTA_SUPPORT
#define OTA_SUPPORT 1
#endif
// При 0, Б (BIN/DRAM/IRAM): ST 19696/1816/0; S3 22160/1808/0.
// Браузерное OTA остаётся при наличии веба и OTA-разделов; heap ArduinoOTA/mDNS/SDK не измерена.
#ifndef ARDUINO_OTA_SUPPORT
#define ARDUINO_OTA_SUPPORT (OTA_SUPPORT && (WIFI_SUPPORT || ETHERNET_SUPPORT))
#endif
// При 0, Б (BIN/DRAM/IRAM): ST 2464/2280/0; S3 1440/2576/0.
// ESP32/C3 ByteLog 2280 Б статически; S2/S3 heap текстов 160 строк до 220 символов + pending, полный runtime не измерен.
#ifndef LOG_HISTORY_SUPPORT
#define LOG_HISTORY_SUPPORT 1
#endif
// При 0, Б (BIN/DRAM/IRAM): ST 2448/1488/0; S3 1984/1488/0.
// RssiHistory 1488 Б статически; отдельного постоянного heap-буфера нет, полный runtime не измерен.
#ifndef RSSI_HISTORY_SUPPORT
#define RSSI_HISTORY_SUPPORT WIFI_SUPPORT
#endif
// При 0, Б (BIN/DRAM/IRAM): ST 2224/0/0; S3 3024/0/0.
// История в NVS; постоянного собственного heap-буфера нет, временный runtime heap не измерен. Защитная история recovery сохраняется.
#ifndef RESET_HISTORY_SUPPORT
#define RESET_HISTORY_SUPPORT 1
#endif
// При 0, Б (BIN/DRAM/IRAM): ST 13072/366/1796; S3 15120/176/0.
// Heap SNTP/UDP/SDK при синхронизации не измерена; монотонные таймеры управления сохраняются.
#ifndef NTP_SUPPORT
#define NTP_SUPPORT (WIFI_SUPPORT || ETHERNET_SUPPORT)
#endif
// При 0, Б (BIN/DRAM/IRAM): ST 125232/824/616; S3 125408/848/256.
// Бюджет 16–24 КиБ, резерв до физического замера 32 КиБ; runtime enabled=false не создаёт клиента. В штатном PIO S2 legacy MQTT уже 0.
// Active: Impl 2464 Б (включает JSON 1024 Б), stack 6144 Б, RX/TX 1024+1024 Б, outbox limit 2048 Б, +32 Б/устройство; SDK/runtime не измерены.
#ifndef MQTT_SUPPORT
#define MQTT_SUPPORT (WIFI_SUPPORT || ETHERNET_SUPPORT)
#endif
// При 0, Б (BIN/DRAM/IRAM): ST 4848/0/0; S3 5200/0/0.
// MQTT-клиент/стек и общий JSON остаются; отдельного подключения нет. NVS/SDK runtime heap не измерена.
#ifndef HOME_ASSISTANT_DISCOVERY_SUPPORT
#define HOME_ASSISTANT_DISCOVERY_SUPPORT MQTT_SUPPORT
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
// При 0, Б (BIN/DRAM/IRAM): ST 0/0/0; S3 0/0/0.
// На Waveshare S3-ETH: 61552/11136/256; на ST/обычной S3 камера уже отсутствует.
// Heap: два JPEG VGA framebuffer в PSRAM + HTTP stack 4096 Б при запуске; размер кадров/полный runtime не измерены.
#ifndef CAMERA_ENABLED
#define CAMERA_ENABLED (SELECTED_BOARD == BOARD_WAVESHARE_S3_ETH && WEB_INTERFACE_ENABLED)
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
#define GRAPHICAL_DISPLAY (DISPLAY_SUPPORT && (DISPLAY_KIND == DISPLAY_ILI9341 || DISPLAY_KIND == DISPLAY_CROWPANEL_21 || DISPLAY_KIND == DISPLAY_GC9A01 || DISPLAY_KIND == DISPLAY_ST7796))

// SPI profiles have a 240-pixel short side; omit unused large font bitmaps.
#define PANEL_COMPACT_FONTS (DISPLAY_KIND == DISPLAY_ILI9341 || DISPLAY_KIND == DISPLAY_GC9A01 || DISPLAY_KIND == DISPLAY_ST7796)
