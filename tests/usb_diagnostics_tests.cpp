#include "diagnostics.hpp"
#include "HWCDC.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <new>
static size_t allocations=0;
void *operator new(size_t n) { ++allocations; if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc(); }
void operator delete(void *p) noexcept {std::free(p);}
void operator delete(void *p,size_t) noexcept {std::free(p);}
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
  char log[STATUS_JSON_CAPACITY];
  assert(diagnostics.copyLog(log,sizeof log)>0);
  assert(std::string_view(log).find("STA reconnect attempt")!=std::string_view::npos);
  // A full, wrapped log must remain readable without heap growth, including
  // after clearing and filling it again with maximum-length fragments.
  const std::string longLine(219,'x');
  const size_t before=allocations;
  for(unsigned pass=0;pass<2;++pass) {
    diagnostics.diagClear();
    assert(diagnostics.copyLog(log,sizeof log)==0);
    for(unsigned i=0;i<1000;++i)diagnostics.dbgView(longLine);
    diagnostics.dbgView("latest",false);
    JsonWriter writer(log,sizeof log);diagnostics.writeLog(writer);
    assert(writer.good() && writer.size()>1000 && writer.size()<=2048+220);
    assert(std::string_view(writer.data(),writer.size()).substr(writer.size()-6)=="latest");
  }
  assert(allocations==before);
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
