#include "input.hpp"
#include <cassert>
#include <iostream>
#include <vector>
using namespace forkrot;

int main() {
  SelectionLineBuffer input;
  std::vector<std::string> commands;
  auto feed = [&](std::string_view bytes) {
    std::string_view line;
    for (char byte : bytes)
      if (input.feed(byte, line)) commands.emplace_back(line);
  };
  // A late suffix is a valid jog by itself; it must remain cancelled.
  feed("M");
  input.select(0);
  feed("R\r\np\n");
  assert((commands == std::vector<std::string>{"p"}));
  commands.clear();

  // More than the old 1024-byte drain limit, consumed in small loop budgets.
  // Fresh bytes appended behind the frozen boundary must still be accepted.
  std::string stale;
  for (unsigned i = 0; i < 800; ++i) stale += "R\n";
  stale += "M";
  input.select(stale.size());
  const auto wire = stale + "R\nP 230 0\np\n";
  for (size_t offset = 0; offset < wire.size(); offset += 128)
    feed(std::string_view(wire).substr(offset, 128));
  assert((commands == std::vector<std::string>{"P 230 0", "p"}));
  commands.clear();

  // Switching again during draining replaces the boundary with all currently
  // queued bytes; the old fragment may end in a later receive call.
  input.select(6);
  feed("R\n");
  input.select(7);
  feed("R\nM");
  feed("123\nP 90 0\n");
  assert((commands == std::vector<std::string>{"P 90 0"}));
  commands.clear();
  input.select(0);
  feed("P 100 0\n");
  assert((commands == std::vector<std::string>{"P 100 0"}));

  // A genuinely new connection starts with a clean framing state.
  feed("M");
  input.select(100);
  input.clear();
  commands.clear();
  feed("p\n");
  assert((commands == std::vector<std::string>{"p"}));
  std::cout << "TCP selection input: fragments, bounded drain, repeated selection, reconnect PASS\n";
}
