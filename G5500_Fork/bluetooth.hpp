// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include "board.hpp"
#include "input.hpp"
#include "packet_queue.hpp"
#include <Arduino.h>
#include <HardwareSerial.h>
#include <memory>
#include <atomic>
#if SELECTED_BOARD == BOARD_ESP32 && BLUETOOTH_SUPPORT
#include <esp_spp_api.h>
#include <esp_gap_bt_api.h>
#endif
namespace forkrot {
#if BLUETOOTH_SUPPORT
class Bluetooth {
public:
  bool begin(uint8_t backend = HardwareConfig{}.backend());
  void end();
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
  void discardInput();
  uint32_t sessionId() const;
  uint32_t droppedCommands() const;
private:
  uint8_t backend_ = BT_NONE;
  uint32_t inputSession_ = 0;
  struct HcState { HardwareSerial serial{1}; LineBuffer receiver; };
  std::unique_ptr<HcState> hc_;
#if SELECTED_BOARD == BOARD_ESP32 && BLUETOOTH_SUPPORT
  static Bluetooth *owner_;
  static void onEvent(esp_spp_cb_event_t event, esp_spp_cb_param_t *param);
  static void onGapEvent(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);
  void onData(const uint8_t *bytes, size_t size);
  struct SppState {
    LineBuffer receiver_;
    std::atomic<bool> ready_{false};
    std::atomic<uint32_t> client_{0}, session_{0};
    std::atomic<bool> writing_{false}, congested_{false};
#ifdef CONFIG_BTDM_CTRL_BR_EDR_MAX_ACL_CONN
    static constexpr unsigned ACL_SLOTS = CONFIG_BTDM_CTRL_BR_EDR_MAX_ACL_CONN;
#else
    static constexpr unsigned ACL_SLOTS = 2;
#endif
    uint32_t aclHandles_[ACL_SLOTS]{};
    std::atomic<unsigned> aclCount_{0};
    uint32_t seenSession_ = 0, receiverSession_ = 0;
    PacketQueue<1024, 8> commands_;
    PacketQueue<512, 8> replies_;
    std::atomic<uint32_t> rxDropped_{0};
  };
  std::unique_ptr<SppState> spp_;
#endif
};
#else
class Bluetooth {
public:
  bool begin(uint8_t backend = BT_NONE) { return backend == BT_NONE; }
  void end() {}
  bool connected() { return false; }
  bool linkConnected() { return false; }
  bool macAddress(char (&address)[18]) const { address[0] = 0; return false; }
  bool receive(char *, size_t, size_t &) { return false; }
  template<class T> bool send(const T &) { return false; }
  void service() {}
  void discardInput() {}
  uint32_t sessionId() const { return 0; }
  uint32_t droppedCommands() const { return 0; }
};
#endif
}
