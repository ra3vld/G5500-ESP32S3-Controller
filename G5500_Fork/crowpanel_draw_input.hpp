// Keep the I2C button serviced between synchronous LVGL software draw tasks.
#pragma once
#include <lvgl.h>
namespace forkrot::crowpanel {
class DrawInput {
  void (*poll_)(void *) = nullptr;
  void *context_ = nullptr;
  bool polling_ = false;
  static void event(lv_event_t *event) {
    const auto code = lv_event_get_code(event);
    if (code != LV_EVENT_DRAW_TASK_ADDED && code != LV_EVENT_DRAW_MAIN_BEGIN &&
        code != LV_EVENT_DRAW_MAIN_END && code != LV_EVENT_DRAW_POST_END) return;
    auto &self = *static_cast<DrawInput *>(lv_event_get_user_data(event));
    if (self.polling_) return;
    self.polling_ = true;
    self.poll_(self.context_);
    self.polling_ = false;
  }
  void attach(lv_obj_t *object) {
    lv_obj_set_send_draw_task_events(object, true);
    lv_obj_add_event_cb(object, event, LV_EVENT_ALL, this);
    for (uint32_t i = 0; i < lv_obj_get_child_count(object); ++i)
      attach(lv_obj_get_child(object, i));
  }
public:
  void begin(lv_obj_t *root, void (*poll)(void *), void *context) {
    poll_ = poll;
    context_ = context;
    attach(root);
  }
};
}
