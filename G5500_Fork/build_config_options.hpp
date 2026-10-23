// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once

// Служебные определения. Пользовательские настройки — в build_config.hpp.
#ifdef ARDUINO
#include <sdkconfig.h>
#endif

#define BOARD_AUTO 0
#define BOARD_S3 1
#define BOARD_S2_LEGACY 2
#define BOARD_S2_MINI 3
#define BOARD_ESP32 4
#define BOARD_CROWPANEL_21 5
#define BOARD_WT32_ETH01 6
#define BOARD_WAVESHARE_S3_ETH 7

#define DISPLAY_NONE 0
#define DISPLAY_ILI9341 1
#define DISPLAY_LCD1602 2
#define DISPLAY_CROWPANEL_21 3

#define DRIVE_YAESU 0
#define DRIVE_SEPARATE_PWM 1
#define DRIVE_BTS7960 2

#define BT_NONE 0
#define BT_HC05 1
#define BT_ESP32_SPP 2
#define BT_AUTO 3

#define LOCAL_INPUT_NONE 0
#define LOCAL_INPUT_BUTTONS 1
#define LOCAL_INPUT_JOYSTICK 2

// Совместимость прежних CrowPanel-сборок; новый флаг -D имеет приоритет.
#if !defined(DRIVE_SIMULATION) && defined(CROWPANEL_SIMULATION)
#define DRIVE_SIMULATION CROWPANEL_SIMULATION
#endif

// Прежний флаг TFT имеет приоритет над значением по умолчанию в файле.
#if !defined(DISPLAY_KIND) && defined(TFT_DISPLAY_ENABLED)
#define DISPLAY_KIND TFT_DISPLAY_ENABLED
#endif
