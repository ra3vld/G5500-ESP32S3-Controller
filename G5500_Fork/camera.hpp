// Optional DVP camera, isolated from the control HTTP server. GPL-3.0.
#pragma once
#include "build_config.hpp"
#include "json_writer.hpp"
namespace forkrot {
class Camera {
public:
  void begin();
  bool ready() const { return ready_; }
  const char *error() const { return error_; }
  void writeStatus(JsonWriter &j) const {
    j << ",\"cameraSupported\":" << (CAMERA_ENABLED?"true":"false")
      << ",\"cameraReady\":" << (ready_?"true":"false")
      << ",\"cameraPort\":" << (ready_?82:0)
      << ",\"cameraError\":\"" << error_ << '"';
  }
private:
  bool ready_=false;
  const char *error_="";
};
}
