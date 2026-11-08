#include "display.hpp"
#if GRAPHICAL_DISPLAY
#include <Arduino.h>
#if DISPLAY_KIND == DISPLAY_CROWPANEL_21
#include "crowpanel_rgb_driver.h"
#endif
namespace forkrot {
void Display::showOta(const OtaProgress &progress) {
  if(!display_ || displayFailed_)return;
  const bool starting=!otaScreen_;
  otaScreen_=true;
  // Finish any pending normal frame before changing its LVGL objects/buffers.
  const auto start=millis();
  while(!serviceFrame()) {
    if(displayFailed_ || uint32_t(millis()-start)>=160)return;
    delay(1);
  }
#if DISPLAY_KIND == DISPLAY_CROWPANEL_21
  // Reserve the large SRAM block before adding any OTA labels/glyph caches.
  if(starting && crowpanel_rgb_ota_frame(panel_,nullptr)!=ESP_OK) {
    displayFailed_=true;disableBacklight();return;
  }
#else
  (void)starting;
#endif
  ui_.showOta(progress);
  rendering_=true;lv_refr_now(display_);rendering_=false;
}
}
#endif
