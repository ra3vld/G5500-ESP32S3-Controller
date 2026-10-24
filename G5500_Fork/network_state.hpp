// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include <Arduino.h>
#include "network_config.hpp"
namespace forkrot {
struct WifiCredentials {
  String wifiSsid, wifiPass, apPass;
  NetworkConfig network;
};
} // namespace forkrot
