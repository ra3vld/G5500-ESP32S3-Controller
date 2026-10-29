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

static_assert(BOARD_PROFILE >= BOARD_AUTO && BOARD_PROFILE <= BOARD_ESP32_3248S035C, "Invalid board profile");
#if BOARD_PROFILE == BOARD_AUTO
#if defined(ARDUINO_WT32_ETH01)
#define SELECTED_BOARD BOARD_WT32_ETH01
#elif defined(ARDUINO_LOLIN_S2_MINI)
#define SELECTED_BOARD BOARD_S2_MINI
#elif CONFIG_IDF_TARGET_ESP32S2
#define SELECTED_BOARD BOARD_S2_LEGACY
#elif CONFIG_IDF_TARGET_ESP32
#define SELECTED_BOARD BOARD_ESP32
#elif CONFIG_IDF_TARGET_ESP32C3
#define SELECTED_BOARD BOARD_ESP32_C3
#else
#define SELECTED_BOARD BOARD_S3
#endif
#else
#define SELECTED_BOARD BOARD_PROFILE
#endif

#define FORK_CLASSIC_ESP32 (SELECTED_BOARD == BOARD_ESP32 || SELECTED_BOARD == BOARD_WT32_ETH01 || SELECTED_BOARD == BOARD_ESP32_3248S035C)
#define FORK_S3 (SELECTED_BOARD == BOARD_S3 || SELECTED_BOARD == BOARD_CROWPANEL_21 || SELECTED_BOARD == BOARD_WAVESHARE_S3_ETH)
#define FORK_C3 (SELECTED_BOARD == BOARD_ESP32_2424S012C || SELECTED_BOARD == BOARD_ESP32_C3)
#define FORK_OPTIONAL_DRIVE (FORK_C3 || SELECTED_BOARD == BOARD_ESP32_3248S035C)

static_assert(WEB_PLAIN_HTML_SUPPORT == 0 || WEB_PLAIN_HTML_SUPPORT == 1, "WEB_PLAIN_HTML_SUPPORT must be 0 or 1");
#if WEB_PLAIN_HTML_SUPPORT && !WEB_INTERFACE_ENABLED
#error "Plain HTML requires the web interface"
#endif

// Feature dependencies are evaluated after resolving the board profile.
static_assert(DISPLAY_SUPPORT == 0 || DISPLAY_SUPPORT == 1, "DISPLAY_SUPPORT must be 0 or 1");
static_assert(LOCAL_INPUT_SUPPORT == 0 || LOCAL_INPUT_SUPPORT == 1, "LOCAL_INPUT_SUPPORT must be 0 or 1");
static_assert(WIFI_SUPPORT == 0 || WIFI_SUPPORT == 1, "WIFI_SUPPORT must be 0 or 1");
static_assert(ETHERNET_SUPPORT == 0 || ETHERNET_SUPPORT == 1, "ETHERNET_SUPPORT must be 0 or 1");
static_assert(WEB_LIVE_SUPPORT == 0 || WEB_LIVE_SUPPORT == 1, "WEB_LIVE_SUPPORT must be 0 or 1");
static_assert(BLUETOOTH_SPP_SUPPORT == 0 || BLUETOOTH_SPP_SUPPORT == 1, "BLUETOOTH_SPP_SUPPORT must be 0 or 1");
static_assert(GS232_SUPPORT == 0 || GS232_SUPPORT == 1, "GS232_SUPPORT must be 0 or 1");
static_assert(ROTCTLD_SUPPORT == 0 || ROTCTLD_SUPPORT == 1, "ROTCTLD_SUPPORT must be 0 or 1");
static_assert(N1MM_SUPPORT == 0 || N1MM_SUPPORT == 1, "N1MM_SUPPORT must be 0 or 1");
static_assert(PSTROTATOR_SUPPORT == 0 || PSTROTATOR_SUPPORT == 1, "PSTROTATOR_SUPPORT must be 0 or 1");
static_assert(COMMAND_API_SUPPORT == 0 || COMMAND_API_SUPPORT == 1, "COMMAND_API_SUPPORT must be 0 or 1");
static_assert(HUD_SUPPORT == 0 || HUD_SUPPORT == 1, "HUD_SUPPORT must be 0 or 1");
static_assert(HUD_UDP_SUPPORT == 0 || HUD_UDP_SUPPORT == 1, "HUD_UDP_SUPPORT must be 0 or 1");
static_assert(HUD_UART_SUPPORT == 0 || HUD_UART_SUPPORT == 1, "HUD_UART_SUPPORT must be 0 or 1");
static_assert(RS485_SUPPORT == 0 || RS485_SUPPORT == 1, "RS485_SUPPORT must be 0 or 1");
static_assert(MODBUS_RTU_SUPPORT == 0 || MODBUS_RTU_SUPPORT == 1, "MODBUS_RTU_SUPPORT must be 0 or 1");
static_assert(OTA_SUPPORT == 0 || OTA_SUPPORT == 1, "OTA_SUPPORT must be 0 or 1");
static_assert(ARDUINO_OTA_SUPPORT == 0 || ARDUINO_OTA_SUPPORT == 1, "ARDUINO_OTA_SUPPORT must be 0 or 1");
static_assert(LOG_HISTORY_SUPPORT == 0 || LOG_HISTORY_SUPPORT == 1, "LOG_HISTORY_SUPPORT must be 0 or 1");
static_assert(RSSI_HISTORY_SUPPORT == 0 || RSSI_HISTORY_SUPPORT == 1, "RSSI_HISTORY_SUPPORT must be 0 or 1");
static_assert(RESET_HISTORY_SUPPORT == 0 || RESET_HISTORY_SUPPORT == 1, "RESET_HISTORY_SUPPORT must be 0 or 1");
static_assert(NTP_SUPPORT == 0 || NTP_SUPPORT == 1, "NTP_SUPPORT must be 0 or 1");
static_assert(MQTT_SUPPORT == 0 || MQTT_SUPPORT == 1, "MQTT_SUPPORT must be 0 or 1");
static_assert(HOME_ASSISTANT_DISCOVERY_SUPPORT == 0 || HOME_ASSISTANT_DISCOVERY_SUPPORT == 1, "HOME_ASSISTANT_DISCOVERY_SUPPORT must be 0 or 1");
#if WEB_LIVE_SUPPORT && !WEB_INTERFACE_ENABLED
#error "WebSocket requires WEB_INTERFACE_ENABLED"
#endif
#if WEB_INTERFACE_ENABLED && !(WIFI_SUPPORT || ETHERNET_SUPPORT)
#error "Web requires Wi-Fi or Ethernet"
#endif
#if MQTT_SUPPORT && !(WIFI_SUPPORT || ETHERNET_SUPPORT)
#error "MQTT requires Wi-Fi or Ethernet"
#endif
#if HOME_ASSISTANT_DISCOVERY_SUPPORT && !MQTT_SUPPORT
#error "Discovery requires MQTT"
#endif
#if BLUETOOTH_SPP_SUPPORT && (!BLUETOOTH_SUPPORT || !FORK_CLASSIC_ESP32)
#error "SPP requires Classic ESP32 Bluetooth"
#endif
#if HUD_UDP_SUPPORT && (!HUD_SUPPORT || !(WIFI_SUPPORT || ETHERNET_SUPPORT))
#error "HUD UDP requires HUD and a network"
#endif
#if HUD_UART_SUPPORT && !HUD_SUPPORT
#error "HUD UART requires HUD"
#endif
#if MODBUS_RTU_SUPPORT && !RS485_SUPPORT
#error "Modbus requires RS485"
#endif
#if ARDUINO_OTA_SUPPORT && (!OTA_SUPPORT || !(WIFI_SUPPORT || ETHERNET_SUPPORT))
#error "Arduino OTA requires OTA and a network"
#endif
#if ETHERNET_SUPPORT && SELECTED_BOARD != BOARD_WT32_ETH01 && SELECTED_BOARD != BOARD_WAVESHARE_S3_ETH
#error "Ethernet is not available on this board"
#endif
#if !COMMAND_API_SUPPORT && !WEB_INTERFACE_ENABLED
#error "Keep a management channel: command API or web"
#endif

#if CAMERA_ENABLED != 0 && CAMERA_ENABLED != 1
#error "CAMERA_ENABLED must be 0 or 1"
#endif
#if CAMERA_ENABLED && (SELECTED_BOARD != BOARD_WAVESHARE_S3_ETH || !WEB_INTERFACE_ENABLED)
#error "Camera requires Waveshare ESP32-S3-ETH and the web interface"
#endif
#if SELECTED_BOARD == BOARD_WAVESHARE_S3_ETH && DISPLAY_KIND != DISPLAY_NONE
#error "Waveshare ESP32-S3-ETH supports DISPLAY_NONE"
#endif

#if SELECTED_BOARD == BOARD_WT32_ETH01 && DISPLAY_KIND != DISPLAY_NONE
#error "WT32-ETH01 supports DISPLAY_NONE; Ethernet and drive reserve the display GPIOs"
#endif

#if !BLUETOOTH_ENABLED
#define BLUETOOTH_BACKEND BT_NONE
#elif BLUETOOTH_MODE != BT_AUTO
#define BLUETOOTH_BACKEND BLUETOOTH_MODE
#elif FORK_CLASSIC_ESP32 && BLUETOOTH_SPP_SUPPORT
#define BLUETOOTH_BACKEND BT_ESP32_SPP
#else
#define BLUETOOTH_BACKEND BT_HC05
#endif

#if BLUETOOTH_MODE < BT_HC05 || BLUETOOTH_MODE > BT_AUTO
#error "Invalid BLUETOOTH_MODE: use BT_AUTO, BT_HC05 or BT_ESP32_SPP"
#endif
#if BLUETOOTH_BACKEND == BT_ESP32_SPP && !FORK_CLASSIC_ESP32
#error "Built-in Bluetooth SPP requires an ESP32 chip"
#endif
#if DRIVE_SIMULATION != 0 && DRIVE_SIMULATION != 1
#error "DRIVE_SIMULATION must be 0 or 1"
#endif

#if DISPLAY_KIND < 0 || DISPLAY_KIND > DISPLAY_ST77916 || DRIVE_INTERFACE < 0 || DRIVE_INTERFACE > DRIVE_DIR_PWM
#error "Unknown display or drive interface"
#endif
#if DISPLAY_KIND == DISPLAY_ST77916
#if SELECTED_BOARD != BOARD_ESP32 && SELECTED_BOARD != BOARD_S3
#error "External ST77916 requires BOARD_ESP32 or BOARD_S3"
#endif
static_assert(ST77916_SPI_HZ >= 1000000 && ST77916_SPI_HZ <= 40000000, "ST77916 SPI clock: 1..40 MHz");
static_assert(ST77916_INIT_VARIANT >= 0 && ST77916_INIT_VARIANT <= 2, "ST77916 panel variant: 0 auto, 1 or 2");
#endif
#if SELECTED_BOARD == BOARD_ESP32_3248S035C
#if DISPLAY_KIND != DISPLAY_ST7796
#error "ESP32-3248S035C requires its built-in ST7796 display"
#endif
#elif DISPLAY_KIND == DISPLAY_ST7796
#error "Built-in ST7796 requires BOARD_ESP32_3248S035C"
#endif
#if SELECTED_BOARD == BOARD_ESP32_C3 && DISPLAY_KIND != DISPLAY_NONE
#error "ESP32-C3 headless profile requires DISPLAY_NONE"
#endif
#if SELECTED_BOARD == BOARD_ESP32_2424S012C
#if DISPLAY_KIND != DISPLAY_GC9A01
#error "ESP32-2424S012C requires its built-in GC9A01 display"
#endif
#elif DISPLAY_KIND == DISPLAY_GC9A01
#error "Built-in GC9A01 requires BOARD_ESP32_2424S012C"
#endif
#if SELECTED_BOARD == BOARD_CROWPANEL_21
#if DISPLAY_KIND != DISPLAY_CROWPANEL_21 || USER_BUTTON_GPIO != -1
#error "CrowPanel requires its RGB display and PCF8574 button (USER_BUTTON_GPIO=-1)"
#endif
#if (!CROWPANEL_LOCAL_DRIVE && DRIVE_INTERFACE != DRIVE_YAESU) || DRIVE_INTERFACE == DRIVE_SEPARATE_PWM || DRIVE_INTERFACE == DRIVE_DIR_PWM
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
#if CROWPANEL_USB_TRACE != 0 && CROWPANEL_USB_TRACE != 1
#error "CROWPANEL_USB_TRACE must be 0 or 1"
#endif
#if CROWPANEL_USB_TRACE && (SELECTED_BOARD != BOARD_CROWPANEL_21 || DISPLAY_KIND != DISPLAY_CROWPANEL_21)
#error "CrowPanel USB trace requires BOARD_CROWPANEL_21 and its display"
#endif
#if CROWPANEL_USB_TRACE && defined(ARDUINO) && (!ARDUINO_USB_MODE || !ARDUINO_USB_CDC_ON_BOOT)
#error "CrowPanel USB trace requires native hardware USB CDC on boot"
#endif
#if CROWPANEL_USB_TRACE && CROWPANEL_TOUCH_TRACE
#error "CrowPanel USB trace owns USB; disable the old CROWPANEL_TOUCH_TRACE text logger"
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
static_assert(LOCAL_INPUT_MODE >= LOCAL_INPUT_NONE && LOCAL_INPUT_MODE <= LOCAL_INPUT_BOTH,
              "Invalid LOCAL_INPUT_MODE");
#if LOCAL_INPUT_MODE & LOCAL_INPUT_BUTTONS
static_assert((LOCAL_AZ_NEG_GPIO == -1) == (LOCAL_AZ_POS_GPIO == -1) &&
              (LOCAL_EL_NEG_GPIO == -1) == (LOCAL_EL_POS_GPIO == -1), "Configure both buttons of each axis");
static_assert(LOCAL_AZ_NEG_GPIO >= 0 || LOCAL_EL_NEG_GPIO >= 0, "Configure at least one local button pair");
#endif
#if LOCAL_INPUT_MODE & LOCAL_INPUT_JOYSTICK
static_assert(LOCAL_AZ_ADC_GPIO >= 0 || LOCAL_EL_ADC_GPIO >= 0, "Configure at least one joystick ADC");
static_assert(LOCAL_ADC_DEADZONE > 0 && LOCAL_ADC_HYSTERESIS >= 0, "Invalid joystick dead zone/hysteresis");
static_assert(LOCAL_AZ_ADC_MIN >= 0 && LOCAL_AZ_ADC_MAX <= 4095 &&
              LOCAL_AZ_ADC_CENTER - LOCAL_AZ_ADC_MIN > LOCAL_ADC_DEADZONE + LOCAL_ADC_HYSTERESIS &&
              LOCAL_AZ_ADC_MAX - LOCAL_AZ_ADC_CENTER > LOCAL_ADC_DEADZONE + LOCAL_ADC_HYSTERESIS &&
              LOCAL_EL_ADC_MIN >= 0 && LOCAL_EL_ADC_MAX <= 4095 &&
              LOCAL_EL_ADC_CENTER - LOCAL_EL_ADC_MIN > LOCAL_ADC_DEADZONE + LOCAL_ADC_HYSTERESIS &&
              LOCAL_EL_ADC_MAX - LOCAL_EL_ADC_CENTER > LOCAL_ADC_DEADZONE + LOCAL_ADC_HYSTERESIS,
              "Invalid joystick ADC calibration");
#endif
static_assert((LOCAL_AZ_INVERT == 0 || LOCAL_AZ_INVERT == 1) &&
              (LOCAL_EL_INVERT == 0 || LOCAL_EL_INVERT == 1), "Local axis inversion must be 0 or 1");
