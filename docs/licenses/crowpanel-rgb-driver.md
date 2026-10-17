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

Local modifications (updated 2026-10-17):

- Compile the implementation only for ESP32-S3.
- Rename the two private includes to their local names.
- Prefix exported RGB API symbols with `crowpanel_` and require ESP-IDF 5.5.5.
- Enable `CONFIG_LCD_RGB_RESTART_IN_VSYNC` for direct DMA. In bounce mode only
  restart after missing EOF events or an explicit request, avoiding forced
  restarts on healthy frames. DMA starvation can otherwise leave the LCD shifted.
- Allocate a separate immutable restart link for every framebuffer. Each link
  skips the S3 FIFO prefix and then continues at node 1 of the same framebuffer.
  The original implementation always restarted framebuffer 0, conflicting with
  LVGL double buffering. Release every allocated restart link on cleanup.
- Allow the intentionally unaligned FIFO-prefix skip in both restart paths.
- Derive the restart length from `gdma_link_get_length(..., 0)`, accounting for
  GDMA node alignment instead of assuming the nominal descriptor maximum.
- Publish a submitted framebuffer only after cache writeback; protect its index
  and tail links with the same spinlock used by the ISR's restart selection.
- Split drawing-time cache writeback into at most 4096 bytes per call, and reject
  publication if any call fails. The pinned Arduino SDK does not enable
  `CONFIG_ESP_MM_CACHE_MSYNC_C2M_CHUNKED_OPS`; a single full-frame writeback holds
  off interrupts and can delay VSYNC recovery into active scanout. See the
  [ESP-IDF cache configuration](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_mm/Kconfig).
- Allocate a dedicated level-3 LCD interrupt instead of sharing a low-priority
  interrupt with other peripherals. H5 also reports the minimum/maximum VSYNC
  interval so delayed interrupts can be distinguished from DMA start failures.
- Expose submitted/presented frame generations under the same spinlock. Advance
  presentation after successful GDMA start in direct mode, or after the final
  old-frame chunk has been copied to SRAM and the next framebuffer is latched
  in bounce mode. Protect a complete EOF refill from the higher-priority VSYNC
  recovery handler; the two handlers must not refill/reset bounce state at once.
- Expose successful restart count, framebuffer mask and DMA start errors through
  `crowpanel_rgb_get_restart_stats()` and the existing H5 diagnostics.

The application uses two PSRAM framebuffers, two 20-row internal SRAM bounce
buffers (38,400 bytes total), and an 8 MHz pixel clock. DMA reads SRAM while
CPU cache supplies the next rows from PSRAM. No PSRAM cache writeback is needed
for this path. Cache-dependent LCD/GDMA interrupts are suspended during flash
writes in the pinned SDK. A board-level compile guard rejects cache-safe ISR
configurations without PSRAM XIP; marking the function itself IRAM is not enough
to make PSRAM access safe with cache disabled.

The `crowpanel_21-dial-2026-10-17` image acknowledges DMA ownership asynchronously
before reusing the preceding framebuffer, allowing animation without blocking the
control loop. The RGB driver itself is unchanged from the visually confirmed
SRAM bounce image. The diagnostic `crowpanel_21-ring-bounce-2026-10-17` image
retains the preceding two-VSYNC wait to isolate the pixel-transfer change from
concurrent main-loop changes. Its manifest preserves the exact compiled sources.

Host tests compile the driver's actual init, draw, restart and refill bodies
against GDMA/cache fakes, including 4/32/64-byte alignment, 1/2/3 framebuffers,
VSYNC during cache writeback, repeated direct-mode recovery, start errors,
healthy bounce frames without restarts, missed-EOF recovery, and old/new pixel
contents across a bounce framebuffer handoff.
They do not emulate panel timing, LCD FIFO contents or physical image quality.

The change does not establish that DMA underruns cannot occur. Runtime counters
confirm recovery execution, while horizontal shift and faint row artifacts need
visual validation on the panel. See the [ESP-IDF RGB documentation](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html#bounce-buffer-only)
for the permanent-shift failure mode and VSYNC recovery.
