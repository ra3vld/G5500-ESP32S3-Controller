#include "camera.hpp"
#include <Arduino.h>
#include <esp_camera.h>
#include <esp_http_server.h>
#include <cassert>
#include <iostream>
using namespace forkrot;
static std::string status(const Camera &camera) {
  char data[300];JsonWriter j(data,sizeof data);camera.writeStatus(j);assert(j.good());return j.data();
}
int main() {
  fakePsram=false;Camera missing;missing.begin();assert(status(missing).find("camera_psram_missing")!=std::string::npos);
  fakePsram=true;fakeCameraInitOk=false;Camera init;init.begin();assert(status(init).find("camera_init_failed")!=std::string::npos);
  fakeCameraInitOk=true;fakeHttpStartOk=false;Camera server;server.begin();assert(fakeCameraDeinits==1 && status(server).find("camera_server_failed")!=std::string::npos);
  fakeHttpStartOk=true;fakeHttpRegisterOk=false;Camera route;route.begin();assert(fakeHttpStops==1 && fakeCameraDeinits==2);
  fakeHttpRegisterOk=true;Camera camera;camera.begin();
  assert(status(camera).find("\"cameraReady\":true")!=std::string::npos);
  assert(fakeCameraConfig.pin_d0==41 && fakeCameraConfig.pin_d7==18 && fakeCameraConfig.pin_xclk==3);
  assert(fakeCameraConfig.ledc_timer==3 && fakeCameraConfig.ledc_channel==7 && fakeCameraConfig.fb_location==CAMERA_FB_IN_PSRAM);
  assert(fakeHttpConfig.max_open_sockets==4 && fakeHttpConfig.server_port==82 && fakeHttpConfig.ctrl_port==32770 && fakeHttpConfig.task_priority==1 && fakeHttpConfig.send_wait_timeout==1);
  assert(std::string(fakeEndpoint.uri)=="/camera.jpg");
  httpd_req_t request;
  assert(fakeEndpoint.handler(&request)==ESP_OK && request.type=="image/jpeg" && request.body.size()==4);
  assert(fakeFrameReturns==1 && request.headers["Cache-Control"]=="no-store" && request.headers.count("Connection")==0);
  assert(fakeEndpoint.handler(&request)==ESP_OK && fakeFrameReturns==2); // persistent next frame
  fakeHttpSendOk=false;assert(fakeEndpoint.handler(&request)==ESP_FAIL);assert(fakeFrameReturns==3);
  fakeHttpSendOk=true;fakeFrame.format=-1;fakeEndpoint.handler(&request);assert(fakeFrameReturns==4 && request.headers["Connection"]=="close" && request.status=="503 Service Unavailable");
  fakeNoFrame=true;fakeEndpoint.handler(&request);assert(fakeFrameReturns==4 && request.body=="camera_capture_failed");
  std::cout << "PASS: camera init failures, isolated HTTP, JPEG ownership and send/capture errors\n";
}
