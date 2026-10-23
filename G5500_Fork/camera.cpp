// Waveshare ESP32-S3-ETH camera snapshots. GPL-3.0.
#include "camera.hpp"
#if CAMERA_ENABLED
#include "board.hpp"
#include <Arduino.h>
#include <esp_camera.h>
#include <esp_http_server.h>

namespace forkrot {
namespace {
esp_err_t capture(httpd_req_t *request) {
  // Only this server task touches frames; the control loop never waits for capture/send.
  httpd_resp_set_hdr(request,"Cache-Control","no-store");
  camera_fb_t *frame=esp_camera_fb_get();
  if(!frame) {
    httpd_resp_set_hdr(request,"Connection","close");
    httpd_resp_set_status(request,"503 Service Unavailable");
    httpd_resp_send(request,"camera_capture_failed",HTTPD_RESP_USE_STRLEN);
    return ESP_FAIL;
  }
  esp_err_t result;
  const bool valid=frame->format==PIXFORMAT_JPEG && frame->len;
  if(!valid) {
    httpd_resp_set_hdr(request,"Connection","close");
    httpd_resp_set_status(request,"503 Service Unavailable");
    result=httpd_resp_send(request,"camera_invalid_frame",HTTPD_RESP_USE_STRLEN);
  } else {
    httpd_resp_set_type(request,"image/jpeg");
    result=httpd_resp_send(request,reinterpret_cast<const char *>(frame->buf),frame->len);
  }
  esp_camera_fb_return(frame); // Also on a disconnected or slow client.
  // Reuse HTTP/1.1 for the next frame; errors close their session.
  return valid ? result : ESP_FAIL;
}
}
void Camera::begin() {
  if(ready_) return;
  if(!psramFound()) { error_="camera_psram_missing"; return; }
  // Waveshare drives CAM_ENABLE LOW; it is not the sensor PWDN signal.
  pinMode(board::CAM_POWER,OUTPUT);
  digitalWrite(board::CAM_POWER,LOW);
  delay(20);
  camera_config_t config{};
  config.pin_pwdn=-1; config.pin_reset=-1;
  config.pin_xclk=board::CAM_XCLK; config.pin_pclk=board::CAM_PCLK;
  config.pin_vsync=board::CAM_VSYNC; config.pin_href=board::CAM_HREF;
  config.pin_sccb_sda=board::CAM_SDA; config.pin_sccb_scl=board::CAM_SCL;
  config.pin_d0=board::CAM_DATA[0]; config.pin_d1=board::CAM_DATA[1];
  config.pin_d2=board::CAM_DATA[2]; config.pin_d3=board::CAM_DATA[3];
  config.pin_d4=board::CAM_DATA[4]; config.pin_d5=board::CAM_DATA[5];
  config.pin_d6=board::CAM_DATA[6]; config.pin_d7=board::CAM_DATA[7];
  config.ledc_timer=LEDC_TIMER_3; config.ledc_channel=LEDC_CHANNEL_7;
  config.xclk_freq_hz=20000000;
  config.pixel_format=PIXFORMAT_JPEG; config.frame_size=FRAMESIZE_VGA;
  config.jpeg_quality=14; config.fb_count=2;
  config.fb_location=CAMERA_FB_IN_PSRAM; config.grab_mode=CAMERA_GRAB_LATEST;
  if(esp_camera_init(&config)!=ESP_OK) {
    digitalWrite(board::CAM_POWER,HIGH);
    error_="camera_init_failed"; return;
  }
  httpd_config_t http=HTTPD_DEFAULT_CONFIG();
  http.server_port=82; http.ctrl_port=32770; // 32769 belongs to the live WebSocket server.
  http.task_priority=1; http.stack_size=4096;
  http.max_open_sockets=4; http.max_uri_handlers=1;
  http.lru_purge_enable=true; http.recv_wait_timeout=1; http.send_wait_timeout=1;
  httpd_handle_t server=nullptr;
  if(httpd_start(&server,&http)==ESP_OK) {
    httpd_uri_t endpoint{};
    endpoint.uri="/camera.jpg"; endpoint.method=HTTP_GET; endpoint.handler=capture;
    if(httpd_register_uri_handler(server,&endpoint)==ESP_OK) {
      ready_=true; error_=""; return;
    }
    httpd_stop(server);
  }
  esp_camera_deinit();
  digitalWrite(board::CAM_POWER,HIGH);
  error_="camera_server_failed";
}
}
#elif SELECTED_BOARD == BOARD_WAVESHARE_S3_ETH
#include "board.hpp"
#include <Arduino.h>
namespace forkrot {
void Camera::begin() {
  pinMode(board::CAM_POWER,OUTPUT);
  digitalWrite(board::CAM_POWER,HIGH);
}
}
#else
namespace forkrot { void Camera::begin() {} }
#endif
