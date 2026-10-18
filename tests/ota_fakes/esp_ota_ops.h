#pragma once
#include "ArduinoOTA.h"
inline constexpr int ESP_PARTITION_TYPE_APP = 0, ESP_PARTITION_TYPE_DATA = 1,
  ESP_PARTITION_SUBTYPE_ANY = 255, ESP_PARTITION_SUBTYPE_DATA_OTA = 0,
  ESP_PARTITION_SUBTYPE_APP_OTA_MIN = 16, ESP_PARTITION_SUBTYPE_APP_OTA_MAX = 32;
struct esp_partition_t { uint32_t address, size; int subtype; };
inline esp_partition_t otaPartitions[] = {{0x10000, 0x1e0000, 16}, {0x1f0000, 0x1e0000, 17}};
inline esp_partition_t otaMetadata{0xe000, 0x2000, 0};
inline unsigned otaPartitionCount = 2;
inline bool otaHasMetadata = true, otaNextIsRunning = false;
using esp_partition_iterator_t = const esp_partition_t *;
inline const esp_partition_t *esp_ota_get_running_partition() { return &otaPartitions[0]; }
inline const esp_partition_t *esp_ota_get_next_update_partition(void *) { return &otaPartitions[otaNextIsRunning ? 0 : 1]; }
inline const esp_partition_t *esp_partition_find_first(int, int, void *) { return otaHasMetadata ? &otaMetadata : nullptr; }
inline esp_partition_iterator_t esp_partition_find(int, int, void *) { return otaPartitionCount ? &otaPartitions[0] : nullptr; }
inline const esp_partition_t *esp_partition_get(esp_partition_iterator_t p) { return p; }
inline esp_partition_iterator_t esp_partition_next(esp_partition_iterator_t p) { return p + 1 < otaPartitions + otaPartitionCount ? p + 1 : nullptr; }
