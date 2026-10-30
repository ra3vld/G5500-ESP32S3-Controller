#include "motion_fixture.hpp"
#include "uart_reply.hpp"
#include <cassert>
#include <iostream>
#include <string>
using namespace forkrot;
int main() {
  static_assert(sizeof(UartReply)==260);
  UartReply tx;
  std::string output,expected;
  size_t available=0,calls=0;
  const auto write=[&](const uint8_t *p,size_t n) {
    assert(n && n<=available);++calls;
    // Model a driver that sometimes makes less progress than advertised.
    const size_t sent=std::min(n,size_t(7));
    output.append(reinterpret_cast<const char *>(p),sent);available-=sent;return sent;
  };
  Controller controller=testController();Protocol protocol(controller);
  controller.feedback(20,30,true,true);controller.setMode(Mode::Auto,0);
  assert(protocol.line("M120",1).result==Result::Ok);controller.tick(1);
  assert(controller.axes[0].wanted);
  const auto reply=protocol.line("C2",2).reply;
  for(unsigned n=0;n<100;++n) {
    if(tx.push(reply))expected+=reply;
    tx.service(available,write);
  }
  assert(!tx.empty() && !calls); // Saturated TX never calls the blocking driver.
  protocol.line("S",3);controller.tick(3);
  assert(!controller.axes[0].wanted && !controller.axes[0].direction);
  // Process STOP regardless of reply capacity; queued replies remain intact.
  while(!tx.empty()) {available=3;tx.service(available,write);}
  assert(output==expected);
  for(unsigned n=0;n<300;++n) {
    const std::string line=std::to_string(n)+"\r\n";
    if(tx.push(line))expected+=line;
    available=11;tx.service(available,write);
  }
  while(!tx.empty()) {available=128;tx.service(available,write);}
  assert(output==expected); // Ring wrap and partial writes keep whole-line order.
  assert(!tx.push(std::string(257,'x')) && tx.empty());
  assert(tx.push(std::string(256,'y')) && !tx.push("z"));
  tx.clear();assert(tx.empty() && tx.push("new\r\n"));
  output.clear();while(!tx.empty()){available=128;tx.service(available,write);}
  assert(output=="new\r\n");
  std::cout<<"PASS: stalled UART, whole-reply overload, STOP, partial sends, wrap and port reset\n";
}
