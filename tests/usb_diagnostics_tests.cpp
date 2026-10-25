#include "diagnostics.hpp"
#include "HWCDC.h"
#include <cassert>
#include <cstdio>
using namespace forkrot;
int main() {
  Storage storage;
  Diagnostics diagnostics(storage);
  diagnostics.beginNativeUsb();
  // The host remains attached after upload, but its full buffer is not read.
  // Startup and retry messages must make progress without any host wait.
  for(unsigned i=0;i<100;++i) {
    diagnostics.dbgPrint("[WIFI] ");
    diagnostics.dbgPrintln("STA reconnect attempt");
    diagnostics.dbgView("USB host is not reading");
  }
  const uint8_t reply[]={1,2,3};
  assert(diagnostics.nativeWrite(reply,sizeof reply)==0);
  assert(fakeUsb.waitedMs==0);
  assert(diagnostics.diagSnapshot().length()>0);
  assert(std::string(diagnostics.diagSnapshot().c_str()).find("STA reconnect attempt")!=std::string::npos);
  // Resuming the reader restores USB output without a reset or log loss.
  fakeUsb.consuming=true;
  diagnostics.dbgPrintln("[WIFI] STA connected");
  assert(fakeUsb.output.find("STA connected")!=std::string::npos);
  assert(diagnostics.nativeWrite(reply,sizeof reply)==sizeof reply);
  diagnostics.readBootReason();
  assert(diagnostics.bootReasonCode()==ESP_RST_USB && diagnostics.bootReasonText()=="USB");
  fakeResetReason=ESP_RST_SW;
  diagnostics.readBootReason();
  assert(diagnostics.bootReasonText()=="SOFTWARE");
  std::puts("PASS: USB host backpressure never waits; RAM log and resumed USB replies survive; USB reset identified");
}
