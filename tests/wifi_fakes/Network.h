#pragma once
struct FakeNetwork {
  bool ready=true;
  bool begin() {return ready;}
};
inline FakeNetwork Network;
