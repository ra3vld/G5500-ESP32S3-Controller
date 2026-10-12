// G5500 Fork; original by UA1CFM, modifications by RA3VLD. GPL-3.0.
#pragma once
#include "control.hpp"
namespace forkrot {
struct Packet {
  Result result = Result::Invalid;
  bool matched = false;
  std::string reply;
};
class Protocol {
  Controller &controller_;
  float pstBaseAzimuth_ = 0;
  bool pstBaseValid_ = false, longPath_ = false;
  std::vector<Target> loadedSequence_;
  uint32_t loadedIntervalMs_ = 1000;
  uint32_t loadedStopEpoch_ = 0;

public:
  std::string rotorName = "G5500";
  explicit Protocol(Controller &c) : controller_(c) {}
  std::string positionReport(float mechanicalAz) const;
  Packet datagram(const std::string &input, uint32_t now);
  Packet line(const std::string &input, uint32_t now, bool tcp = false);
  void resetPst() {
    pstBaseValid_ = false;
    longPath_ = false;
  }
};
} // namespace forkrot
