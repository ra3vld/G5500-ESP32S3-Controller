// CrowPanel-only names for the pinned ESP-IDF 5.5.5 RGB driver.
#pragma once
#include <esp_idf_version.h>
#if ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(5, 5, 5)
#error "CrowPanel RGB driver is pinned to ESP-IDF 5.5.5; review before upgrading"
#endif
#define esp_lcd_new_rgb_panel crowpanel_esp_lcd_new_rgb_panel
#define esp_lcd_rgb_panel_register_event_callbacks crowpanel_esp_lcd_rgb_panel_register_event_callbacks
#define esp_lcd_rgb_panel_set_pclk crowpanel_esp_lcd_rgb_panel_set_pclk
#define esp_lcd_rgb_panel_restart crowpanel_esp_lcd_rgb_panel_restart
#define esp_lcd_rgb_panel_get_frame_buffer crowpanel_esp_lcd_rgb_panel_get_frame_buffer
#define esp_lcd_rgb_panel_refresh crowpanel_esp_lcd_rgb_panel_refresh
#define esp_lcd_rgb_panel_set_yuv_conversion crowpanel_esp_lcd_rgb_panel_set_yuv_conversion
#include <esp_lcd_panel_rgb.h>
