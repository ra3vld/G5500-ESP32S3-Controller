#include "motion_fixture.hpp"
#include "bluetooth.hpp"
#include "protocol.hpp"
#include <iostream>
#include <stdexcept>
#if BLUETOOTH_BACKEND == BT_ESP32_SPP
#include <thread>
#endif
using namespace forkrot;
unsigned checks = 0;
#define CHECK(expr) do { ++checks; if (!(expr)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " #expr); } while (0)
int main() { try {
  Bluetooth bt;
  std::string command;
  char mac[18];
#if BLUETOOTH_BACKEND == BT_ESP32_SPP
  CHECK(bt.begin()); CHECK(fakeSpp.name == "G5500-Rotator");
  CHECK(!fakeSpp.input && !fakeSpp.output && !fakeSpp.master && fakeSpp.bleDisabled);
  CHECK(fakeSpp.callbackMode && fakeSpp.starts == 1);
  esp_bt_gap_cb_param_t gap; fakeGapCallback(ESP_BT_GAP_CFM_REQ_EVT, &gap);
  CHECK(fakeSpp.pairingConfirmed);
  CHECK(!bt.connected()); CHECK(!bt.send("reply")); CHECK(fakeHardware.events.empty());
  CHECK(bt.macAddress(mac) && std::string(mac) == "02:0A:B0:C1:00:FF");
  fakeSpp.addressAvailable = false; CHECK(!bt.macAddress(mac) && mac[0] == 0);
  fakeSpp.addressAvailable = true;
  CHECK(!bt.linkConnected());
  aclConnect(0, false); CHECK(!bt.linkConnected());
  aclClose(99); CHECK(!bt.linkConnected());
  aclConnect(0); CHECK(bt.linkConnected() && !bt.connected() && !bt.receive(command));
  aclConnect(0); aclClose(0); CHECK(!bt.linkConnected()); // repeated events do not leak a link
  aclConnect(0); aclConnect(7); aclClose(0); CHECK(bt.linkConnected());
  aclClose(99); CHECK(bt.linkConnected());
  aclClose(7); CHECK(!bt.linkConnected());
  // Reused handles and reconnects work without receiving a single SPP byte.
  aclConnect(7); CHECK(bt.linkConnected() && !bt.connected());
  aclClose(7); CHECK(!bt.linkConnected());
  esp_spp_cb_param_t failedOpen; failedOpen.srv_open.status = -1; failedOpen.srv_open.handle = 1;
  fakeSpp.callback(ESP_SPP_SRV_OPEN_EVT, &failedOpen); CHECK(!bt.connected());
  sppConnect(7); CHECK(bt.connected());
  CHECK(bt.linkConnected() && !bt.receive(command)); // idle channel is connected before data
  sppConnect(8); CHECK(bt.connected() && fakeSpp.disconnected.back() == 8);
  sppFeed("M180\r", 8); CHECK(!bt.receive(command));
  fakeSpp.client = 7;
  sppFeed("C\r\nC2\n"); CHECK(bt.receive(command) && command == "C");
  CHECK(bt.receive(command) && command == "C2"); CHECK(!bt.receive(command));
  sppFeed(std::string(513, 'M') + "\rC\r"); CHECK(bt.receive(command) && command == "C");
  sppFeed(std::string("M90\x01\rS\r")); CHECK(bt.receive(command) && command == "S");
  sppFeed(std::string(512, 'C') + "\r"); CHECK(bt.receive(command) && command.size() == 512);
  for (int i = 0; i < 8; ++i) sppFeed("C\r");
  sppFeed("M090\r"); CHECK(bt.droppedCommands() == 1);
  for (int i = 0; i < 8; ++i) CHECK(bt.receive(command) && command == "C");
  CHECK(!bt.receive(command)); sppFeed("C2\r"); CHECK(bt.receive(command) && command == "C2");
  // Byte capacity is independent of the eight packet descriptors.
  const auto fullQueueDrops = bt.droppedCommands();
  sppFeed(std::string(512, 'A') + "\r" + std::string(512, 'B') + "\rC\r");
  CHECK(bt.droppedCommands() == fullQueueDrops + 1);
  char receivedBytes[512]; size_t receivedSize = 0;
  CHECK(bt.receive(receivedBytes, sizeof receivedBytes, receivedSize) && receivedSize == 512 && receivedBytes[0] == 'A');
  CHECK(bt.receive(receivedBytes, sizeof receivedBytes, receivedSize) && receivedSize == 512 && receivedBytes[0] == 'B');
  CHECK(!bt.receive(command));
  CHECK(bt.send("one")); CHECK(fakeSpp.writes.size() == 1);
  CHECK(fakeSpp.writes[0].first == 7 && fakeSpp.writes[0].second == "one");
  for (int i = 0; i < 8; ++i) CHECK(bt.send("queued"));
  CHECK(!bt.send("overflow")); CHECK(fakeSpp.writes.size() == 1);
  // A congested transmitter does not prevent command processing, including STOP.
  Controller controller = forkrot::testController(); controller.feedback(20, 30, true, true); Protocol protocol(controller);
  CHECK(protocol.line("R", 0).result == Result::Ok); CHECK(controller.moving());
  sppFeed("S\r"); CHECK(bt.receive(command)); CHECK(protocol.line(command, 1).result == Result::Ok);
  CHECK(!controller.moving());
  sppComplete(7, true); bt.service(); CHECK(fakeSpp.writes.size() == 1);
  sppCongestion(7, false); bt.service(); CHECK(fakeSpp.writes.size() == 2);
  sppFeed("M0"); CHECK(!bt.receive(command));
  sppFeed("90\r"); sppClose(7); CHECK(!bt.connected());
  CHECK(!bt.linkConnected());
  sppConnect(9); bt.service(); CHECK(fakeSpp.writes.size() == 2); // old TX queue discarded
  sppFeed("C\r"); CHECK(bt.receive(command) && command == "C");
  CHECK(bt.send("new peer")); CHECK(fakeSpp.writes.back().first == 9);
  sppFeed("M0"); CHECK(!bt.receive(command));
  sppClose(42, false); sppFeed("90\r"); CHECK(bt.receive(command) && command == "M090");
  sppClose(9); sppConnect(10); sppComplete(10); // reconnect within one main-loop iteration
  sppFeed("C2\r"); CHECK(bt.receive(command) && command == "C2");
  sppFeed("M090\r"); sppClose(10); sppConnect(11); sppFeed("C\r");
  CHECK(bt.receive(command) && command == "C"); // discard queued commands from the previous peer
  sppCongestion(11, true);
  CHECK(bt.send(std::string_view(std::string(512, 'x'))));
  CHECK(!bt.send("x")); // total TX bytes, even though there are unused slots
  sppClose(11); sppConnect(12); bt.service();
  CHECK(bt.send("fresh")); CHECK(fakeSpp.writes.back().first == 12 && fakeSpp.writes.back().second == "fresh");
  const auto droppedBefore = bt.droppedCommands();
  std::atomic<bool> finished{false};
  std::thread producer([&] {
    for (int i = 0; i < 2000; ++i) sppFeed("C" + std::to_string(i) + "\r");
    finished.store(true);
  });
  unsigned received = 0; int last = -1;
  auto consume = [&] {
    while (bt.receive(command)) {
      CHECK(command[0] == 'C'); int value = std::stoi(command.substr(1));
      CHECK(value > last); last = value; ++received;
    }
  };
  while (!finished.load()) { consume(); std::this_thread::yield(); }
  producer.join(); consume();
  CHECK(received + bt.droppedCommands() - droppedBefore == 2000);
  fakeSpp.beginOk = false; Bluetooth failed; CHECK(!failed.begin());
  CHECK(!failed.connected()); CHECK(!failed.receive(command)); CHECK(!failed.send("reply"));
  fakeSpp.beginOk = true; fakeSpp.startOk = false;
  const auto startupBegin = fakeHardware.now;
  Bluetooth failedServer; CHECK(!failedServer.begin());
  CHECK(!failedServer.connected() && uint32_t(fakeHardware.now - startupBegin) == 1500);
#elif BLUETOOTH_BACKEND == BT_HC05
  CHECK(bt.begin()); CHECK(fakeUart.number == 1 && fakeUart.baud == 9600);
  CHECK(fakeUart.rxPin == board::PIN_HC05_RX && fakeUart.txPin == board::PIN_HC05_TX);
  CHECK(!bt.connected()); fakeHardware.levels[board::PIN_HC05_STATE] = HIGH; CHECK(bt.connected());
  CHECK(bt.linkConnected()); CHECK(!bt.macAddress(mac) && mac[0] == 0);
  for (char byte : std::string("C2\r\n")) fakeUart.rx.push_back(byte);
  CHECK(bt.receive(command) && command == "C2"); CHECK(!bt.receive(command));
  CHECK(bt.send("position\r\n")); CHECK(fakeUart.tx == "position\r\n");
#else
  CHECK(bt.begin()); CHECK(!bt.connected()); CHECK(!bt.receive(command));
  CHECK(!bt.linkConnected()); CHECK(!bt.macAddress(mac) && mac[0] == 0);
  CHECK(!bt.send("reply")); bt.service(); CHECK(fakeHardware.events.empty());
#endif
  std::cout << "PASS: " << checks << " Bluetooth transport checks (backend " << board::BLUETOOTH_BACKEND_NAME << ")\n";
} catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; } }
