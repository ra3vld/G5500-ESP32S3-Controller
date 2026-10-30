#pragma once
#include <cstdint>
using esp_err_t = int;
#ifndef ESP_OK
#define ESP_OK 0
#endif
inline constexpr esp_err_t ESP_FAIL=-1, ESP_ERR_INVALID_SIZE=0x104, ESP_ERR_INVALID_CRC=0x109;
inline constexpr esp_err_t ESP_ERR_NVS_NOT_FOUND=0x1102, ESP_ERR_NVS_TYPE_MISMATCH=0x1103,
  ESP_ERR_NVS_NOT_ENOUGH_SPACE=0x1105, ESP_ERR_NVS_INVALID_LENGTH=0x110c;
inline const char *esp_err_to_name(esp_err_t error) {
  switch(error) {
    case ESP_OK:return "ESP_OK";
    case ESP_FAIL:return "ESP_FAIL";
    case ESP_ERR_INVALID_SIZE:return "ESP_ERR_INVALID_SIZE";
    case ESP_ERR_INVALID_CRC:return "ESP_ERR_INVALID_CRC";
    case ESP_ERR_NVS_NOT_FOUND:return "ESP_ERR_NVS_NOT_FOUND";
    case ESP_ERR_NVS_TYPE_MISMATCH:return "ESP_ERR_NVS_TYPE_MISMATCH";
    case ESP_ERR_NVS_NOT_ENOUGH_SPACE:return "ESP_ERR_NVS_NOT_ENOUGH_SPACE";
    case ESP_ERR_NVS_INVALID_LENGTH:return "ESP_ERR_NVS_INVALID_LENGTH";
    default:return "UNKNOWN ERROR";
  }
}
