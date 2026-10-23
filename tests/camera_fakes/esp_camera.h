#pragma once
#include <cstddef>
#include <cstdint>
using esp_err_t=int;
inline constexpr int ESP_OK=0, ESP_FAIL=-1;
inline constexpr int LEDC_TIMER_3=3, LEDC_CHANNEL_7=7, PIXFORMAT_JPEG=4, FRAMESIZE_VGA=10;
inline constexpr int CAMERA_FB_IN_PSRAM=1,CAMERA_GRAB_LATEST=1;
struct camera_config_t {
  int pin_pwdn,pin_reset,pin_xclk,pin_pclk,pin_vsync,pin_href,pin_sccb_sda,pin_sccb_scl;
  int pin_d0,pin_d1,pin_d2,pin_d3,pin_d4,pin_d5,pin_d6,pin_d7;
  int ledc_timer,ledc_channel,xclk_freq_hz,pixel_format,frame_size,jpeg_quality,fb_count,fb_location,grab_mode;
};
struct camera_fb_t { uint8_t *buf; size_t len; int format; };
inline camera_config_t fakeCameraConfig{};
inline bool fakeCameraInitOk=true,fakeNoFrame=false;
inline unsigned fakeFrameReturns=0,fakeCameraDeinits=0;
inline uint8_t fakeJpeg[]={0xff,0xd8,0xff,0xd9};
inline camera_fb_t fakeFrame{fakeJpeg,sizeof fakeJpeg,PIXFORMAT_JPEG};
inline int esp_camera_init(const camera_config_t *config) { fakeCameraConfig=*config;return fakeCameraInitOk?ESP_OK:ESP_FAIL; }
inline int esp_camera_deinit() { ++fakeCameraDeinits; return ESP_OK; }
inline camera_fb_t *esp_camera_fb_get() { return fakeNoFrame?nullptr:&fakeFrame; }
inline void esp_camera_fb_return(camera_fb_t *) { ++fakeFrameReturns; }
