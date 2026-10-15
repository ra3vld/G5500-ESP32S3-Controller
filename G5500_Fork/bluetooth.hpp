// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include "board.hpp"
#include "input.hpp"
#include "packet_queue.hpp"
#include <Arduino.h>
#if BLUETOOTH_BACKEND == BT_ESP32_SPP
#include <esp_spp_api.h>
#include <esp_gap_bt_api.h>
#include <atomic>
#elif BLUETOOTH_BACKEND == BT_HC05
#include <HardwareSerial.h>
#endif
namespace forkrot {
class Bluetooth {
public:
  bool begin();
  bool connected();
  bool linkConnected();
  bool macAddress(char (&address)[18]) const;
  bool receive(char *out, size_t capacity, size_t &size);
  bool receive(std::string &command) {
    char bytes[512]; size_t size;
    if (!receive(bytes, sizeof bytes, size)) return false;
    command.assign(bytes, size); return true;
  }
  bool send(std::string_view reply);
  bool send(const String &reply) { return send(std::string_view(reply.c_str(), reply.length())); }
  bool send(const char *reply) { return send(std::string_view(reply)); }
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
#if BLUETOOTH_BACKEND == BT_ESP32_SPP
  std::atomic<bool> ready_{false};
#else
  bool ready_ = false;
#endif
#endif
#if BLUETOOTH_BACKEND == BT_HC05
  HardwareSerial serial_{1};
#elif BLUETOOTH_BACKEND == BT_ESP32_SPP
  static Bluetooth *owner_;
  static void onEvent(esp_spp_cb_event_t event, esp_spp_cb_param_t *param);
  static void onGapEvent(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);
  void onData(const uint8_t *bytes, size_t size);
  std::atomic<uint32_t> client_{0}, session_{0};
  std::atomic<bool> writing_{false}, congested_{false};
  // Only GAP callbacks touch the handles; the main loop reads the atomic count.
#ifdef CONFIG_BTDM_CTRL_BR_EDR_MAX_ACL_CONN
  static constexpr unsigned ACL_SLOTS = CONFIG_BTDM_CTRL_BR_EDR_MAX_ACL_CONN;
#else
  static constexpr unsigned ACL_SLOTS = 2;
#endif
  uint32_t aclHandles_[ACL_SLOTS]{}; // handle + 1; zero means unused
  std::atomic<unsigned> aclCount_{0};
  uint32_t seenSession_ = 0;
  PacketQueue<1024, 8> commands_;
  PacketQueue<512, 8> replies_;
  std::atomic<uint32_t> rxDropped_{0};

#endif
};
}
