// Replay short button pulses during production LVGL software rendering.
#include "motion_fixture.hpp"
#include "hud.hpp"
#include "hud_input.hpp"
#include "panel_ui.hpp"
#include "crowpanel_draw_input.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace forkrot;
struct Replay {
  Controller controller = testController();
  Protocol protocol{controller};
  Hud hud{controller, protocol};
  HudKnob knob{hud, controller};
  uint32_t now = 100, last = 0, polls = 0;
  unsigned tasks = 0;
  bool alternateAxisSeen = false;
  static void poll(void *context) {
    auto &self = *static_cast<Replay *>(context);
    if (uint32_t(self.now - self.last) < 5) return;
    self.last = self.now; ++self.polls;
    self.hud.now = self.now;
    const bool down = (self.now >= 150 && self.now < 210) ||
                      (self.now >= 260 && self.now < 320);
    self.knob.sample(true, down, 0, self.now);
    if (down) {
      assert(self.knob.axis() == 1);
      self.alternateAxisSeen = true;
    }
  }
  static void drawCost(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_DRAW_TASK_ADDED) return;
    auto &self = *static_cast<Replay *>(lv_event_get_user_data(event));
    self.now += 5; ++self.tasks;
  }
  void timed(lv_obj_t *object) {
    lv_obj_set_send_draw_task_events(object, true);
    lv_obj_add_event_cb(object, drawCost, LV_EVENT_DRAW_TASK_ADDED, this);
    for (uint32_t i = 0; i < lv_obj_get_child_count(object); ++i)
      timed(lv_obj_get_child(object, i));
  }
};
static std::array<uint16_t,480*480> buffer;
static void flush(lv_display_t *display,const lv_area_t *,uint8_t *) {lv_display_flush_ready(display);}
int main(int argc,char **argv) {
  const bool duringDraw = argc == 1 || std::strcmp(argv[1],"--without-draw-poll");
  Replay replay;
  replay.hud.config.role = DeviceRole::Hud;
  replay.hud.config.simulationEnabled = false;
  replay.hud.begin(104,1004);
  auto *first=replay.hud.add(101,{101,4534,false},"First",true);assert(first);
  first->received=true;first->receivedAt=replay.now-4000;
  first->state.model=Model::G5500; // A previously seen, now offline dual-axis peer.
  assert(replay.hud.add(102,{102,4534,false},"Second",true));
  assert(replay.hud.add(103,{103,4534,false},"Third",true));
  replay.hud.now = replay.now; assert(replay.hud.select(101));
  Replay::poll(&replay);
  lv_init();
  auto *display = lv_display_create(480,480);
  lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display,buffer.data(),nullptr,sizeof buffer,LV_DISPLAY_RENDER_MODE_DIRECT);
  lv_display_set_flush_cb(display,flush);
  PanelUi ui;ui.create(true);
  PanelView view;view.hud=true;view.enabled=false;view.dualAxis=true;
  std::strcpy(view.hudName,"First");std::strcpy(view.hudAddress,"192.0.2.1");ui.update(view);
  replay.timed(ui.root());
  crowpanel::DrawInput input;
  if(duringDraw)input.begin(ui.root(),Replay::poll,&replay);
  replay.now=140;Replay::poll(&replay);
  lv_refr_now(display);
  const uint32_t drawElapsed=replay.now-140;
  for(;replay.now<=600;replay.now+=5)Replay::poll(&replay);
  uint64_t pixelHash=14695981039346656037ull;
  for(uint16_t pixel:buffer){pixelHash^=pixel;pixelHash*=1099511628211ull;}
  std::printf("draw tasks=%u, frame=%u ms, polls=%u, selected=%llu\n",replay.tasks,drawElapsed,replay.polls,
              static_cast<unsigned long long>(replay.hud.config.selected));
  std::printf("pixels=%016llx\n",static_cast<unsigned long long>(pixelHash));
  assert(drawElapsed>=130 && replay.tasks>=26);
  assert(replay.hud.config.selected==102 && replay.knob.selectionChanged && !replay.knob.menu);
  assert(!replay.controller.moving() && !replay.controller.axes[0].tracking);
  assert(replay.alternateAxisSeen && replay.knob.axis()==0);
  lv_display_delete(display);
  std::puts("PASS: short PCF-style button samples survive a slow LVGL frame, cycle an offline HUD peer and never start motion");
}
