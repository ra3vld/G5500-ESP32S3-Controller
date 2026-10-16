# RGB driver used by CrowPanel

The CrowPanel profile uses a namespaced copy of Espressif's RGB LCD driver from
the ESP-IDF `v5.5.5` tag, matching the pinned Arduino core SDK. Copyright and SPDX
headers are preserved. License: [Apache 2.0](ESP-IDF-Apache-2.0.txt).

Upstream files and original SHA-256:

| Local file in `G5500_Fork/` | Upstream | Original SHA-256 |
| --- | --- | --- |
| `crowpanel_rgb_driver.c` | [esp_lcd_panel_rgb.c](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_lcd/rgb/esp_lcd_panel_rgb.c) | `785d6376b61490e1c29f757039a0e7eb3c3e3dd24b68f584a9f6f220f2dccc2d` |
| `crowpanel_rgb_common.h` | [esp_lcd_common.h](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_lcd/priv_include/esp_lcd_common.h) | `bbff1d2b72f1f6b255112557a45fef28ce3be5d652541f1203fdc403225b085c` |
| `crowpanel_rgb_rotation.h` | [rgb_lcd_rotation_sw.h](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_lcd/rgb/rgb_lcd_rotation_sw.h) | `c623989c33f0d43015cd215f79490b95850f02212dece6e0941a23a591bcdf9b` |

Local modifications (2026-10-16):

- Compile the implementation only for ESP32-S3.
- Rename the two private includes to their local names.
- Include `crowpanel_rgb_driver.h`, which prefixes exported RGB API symbols with
  `crowpanel_` and requires the exact ESP-IDF version 5.5.5. Other display drivers
  continue to use SDK symbols.
- Locally disable `CONFIG_LCD_RGB_RESTART_IN_VSYNC`. In this SDK it is enabled,
  but the ESP32-S3 restart descriptor is mounted on `fbs[0]` and linked to
  `dma_fb_links[0]`. Restarting it on every VSYNC conflicts with double-buffer
  rendering. Normal DMA chaining selects the submitted framebuffer correctly.

The application uses two PSRAM framebuffers, no bounce buffers, an 8 MHz pixel
clock and a two-VSYNC wait before reusing the preceding framebuffer. It does
not call the driver's explicit restart API, whose upstream implementation still
uses the first framebuffer. Automatic bounce-buffer recovery is not used.

This workaround does not establish that DMA underruns cannot occur. Visual
validation on the panel remains separate from build and VSYNC counter checks.
