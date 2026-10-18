#pragma once
#include <cstdint>
inline constexpr unsigned ESP_IMAGE_HEADER_MAGIC = 0xe9, CONFIG_IDF_FIRMWARE_CHIP_ID = 0;
struct esp_image_header_t { uint8_t magic = 0, pad[11]{}; uint16_t chip_id = 0; uint8_t tail[10]{}; };
struct esp_image_segment_header_t { uint32_t address, size; };
