#include "panel_menu.hpp"
#include "panel_access_point.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
using namespace forkrot;
int main() {
  char uptime[48];panelUptime(uptime,sizeof uptime,2*86400+3*3600+4*60+5);
  assert(!std::strcmp(uptime,"2д 03:04:05"));
  panelUptime(uptime,sizeof uptime,1000ULL*86400);assert(!std::strcmp(uptime,"1000д 00:00:00"));
  for(bool busy:{false,true})for(bool required:{false,true})for(bool station:{false,true})for(bool wired:{false,true}) {
    const PanelApPolicy policy{true,busy,required,station,wired,true};
    const auto expected=busy?PanelApResult::Busy:required?PanelApResult::Required:!station&&!wired?PanelApResult::NoStation:PanelApResult::Idle;
    assert(policy.check(false)==expected);
    assert(policy.check(true)==(busy?PanelApResult::Busy:PanelApResult::Idle));
  }
  assert((PanelApPolicy{}).check(true)==PanelApResult::Unavailable);
  assert((PanelApPolicy{true,false,false,true,false,false}).check(true)==PanelApResult::LowMemory);
  PanelMenuView a,b;assert(a==b);b.epoch=1;assert(!(a==b));
  b=a;std::strcpy(b.info,"Нет связи");assert(!(a==b));
  std::puts("PASS: panel menu uptime, AP protection during motion/recovery, uplink availability and memory limits");
}
