// G5500 Fork; original by UA1CFM, modifications by RA3VLD. GPL-3.0.
#pragma once
#include <Arduino.h>
namespace forkrot {
struct WifiCredentials {
  String wifiSsid, wifiPass, apPass;
};
} // namespace forkrot
