// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include "board.hpp"
#include "input.hpp"
#include <Arduino.h>
#if BLUETOOTH_BACKEND == BT_ESP32_SPP
#include <BluetoothSerial.h>
#include <atomic>
#elif BLUETOOTH_BACKEND == BT_HC05
#include <HardwareSerial.h>
#endif
namespace forkrot {
class Bluetooth {
public:
  bool begin();
  bool connected();
  bool receive(std::string &command);
  bool send(const String &reply);
  void service();
  uint32_t droppedCommands() const {
#if BLUETOOTH_BACKEND == BT_ESP32_SPP
    return rxDropped_.load();
#else
    return 0;
#endif
  }
private:
#if BLUETOOTH_BACKEND != BT_NONE
  LineBuffer receiver_;
  bool ready_ = false;
#endif
#if BLUETOOTH_BACKEND == BT_HC05
  HardwareSerial serial_{1};
#elif BLUETOOTH_BACKEND == BT_ESP32_SPP
  BluetoothSerial serial_;
  static Bluetooth *owner_;
  static void onEvent(esp_spp_cb_event_t event, esp_spp_cb_param_t *param);
  std::atomic<uint32_t> client_{0}, session_{0};
  std::atomic<bool> writing_{false}, congested_{false};
  uint32_t seenSession_ = 0;
  struct RxLine { std::string command; uint32_t session = 0; };
  // One producer (Bluetooth callback), one consumer (the main loop).
  static constexpr unsigned RX_SLOTS = 8;
  RxLine commands_[RX_SLOTS];
  std::atomic<uint32_t> rxHead_{0}, rxTail_{0}, rxDropped_{0};
  static constexpr unsigned TX_SLOTS = 8;
  String replies_[TX_SLOTS];
  unsigned txHead_ = 0, txCount_ = 0;
#endif
};
}
