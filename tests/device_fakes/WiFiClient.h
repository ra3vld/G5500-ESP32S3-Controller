#pragma once
class WiFiClient {
public:
  operator bool() const {return false;}
  bool connected() const {return false;}
};
