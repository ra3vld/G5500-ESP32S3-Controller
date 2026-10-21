# RGB driver used by CrowPanel

[Документация](../index.md)

The CrowPanel profile uses a namespaced copy of Espressif's RGB LCD driver from
the ESP-IDF `v5.5.5` tag, matching the pinned Arduino core SDK. Copyright and SPDX
headers are preserved. License: [Apache 2.0](ESP-IDF-Apache-2.0.txt).

Upstream files and original SHA-256:

| Local file in `G5500_Fork/` | Upstream | Original SHA-256 |
| --- | --- | --- |
| `crowpanel_rgb_driver.c` | [esp_lcd_panel_rgb.c](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_lcd/rgb/esp_lcd_panel_rgb.c) | `785d6376b61490e1c29f757039a0e7eb3c3e3dd24b68f584a9f6f220f2dccc2d` |
| `crowpanel_rgb_common.h` | [esp_lcd_common.h](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_lcd/priv_include/esp_lcd_common.h) | `bbff1d2b72f1f6b255112557a45fef28ce3be5d652541f1203fdc403225b085c` |
| `crowpanel_rgb_rotation.h` | [rgb_lcd_rotation_sw.h](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_lcd/rgb/rgb_lcd_rotation_sw.h) | `c623989c33f0d43015cd215f79490b95850f02212dece6e0941a23a591bcdf9b` |

Local files are modified by RA3VLD for the G5500 Controller CrowPanel profile.
Changes include namespaced public symbols, framebuffer handoff and restart fixes,
DMA/cache alignment handling, SRAM bounce-buffer recovery and display diagnostics.
Original copyright, SPDX notices and the Apache 2.0 license are retained in the
distributed sources.

The CrowPanel UI uses DejaVu fonts; see [the DejaVu license](DejaVu.txt).
