// G5500 Fork; original by UA1CFM. GPL-3.0.
#include "display.hpp"
#if DISPLAY_KIND == DISPLAY_LCD1602
#include <cstdio>
#include <cmath>
#include <cstring>
namespace forkrot {
void Display::initLocalDisplay() {
  const auto &p=runtime_.controller.hardware.pins;
  lcd_.emplace(p.lcdRs,p.lcdE,p.lcdD4,p.lcdD5,p.lcdD6,p.lcdD7);
  lcd_->begin(16, 2);
  for (uint8_t width=1; width<=5; ++width) {
    uint8_t pixels[8];
    for (auto &row : pixels) row = uint8_t(0x1f << (5-width)) & 0x1f;
    lcd_->createChar(width, pixels);
  }
}
void Display::updateLocalDisplay() {
  const uint32_t now = millis();
  if (!redraw_ && uint32_t(now-updatedAt_) < 20) return;
  const bool force = redraw_;
  redraw_ = false; updatedAt_ = now;
  for (unsigned i=0; i<2; ++i) {
    char text[40];
    bool showBar=true;
    const auto &axis = runtime_.controller.axes[i];
    const char mode = runtime_.controller.pendingValid ? 'W' : runtime_.controller.mode == Mode::Manual ? 'M' : runtime_.controller.mode == Mode::AutoHour ? 'H' : 'A';
    if (runtime_.hud.isHud() && !runtime_.hud.available()) {
      std::snprintf(text, sizeof text, "%-16.16s",
                    i ? (runtime_.hud.active() ? runtime_.hud.active()->label : "Select via Web")
                      : "HUD OFFLINE");
    } else if ((now / 4000) % 3 == 2 && (!i || runtime_.controller.model == Model::G5500)) {
      showBar=false;
      std::snprintf(text,sizeof text,"L%.6gR%.6g",double(runtime_.controller.range(i).start),double(runtime_.controller.range(i).end));
    } else if (i && runtime_.controller.model != Model::G5500) {
      std::snprintf(text, sizeof text, "%s %-9s", runtime_.controller.pendingValid ? "WAIT" : "MODE", Controller::modeName(runtime_.controller.mode));
    } else {
      char target[12] = "---";
      const auto &c = runtime_.controller;
      const bool waiting = c.pendingValid && (i ? c.pending.hasEl : c.pending.hasAz);
      if (waiting || c.targetKnown) std::snprintf(target, sizeof target, "%3d", int(lroundf(waiting ? (i ? c.pending.el : c.pending.az) : axis.target)));
      char current[16] = "NC";
      if (axis.calibrated) std::snprintf(current, sizeof current, "%d", int(lroundf(axis.current)));
      std::snprintf(text, sizeof text, "%c%3s>%3s %3d%%%c%c", i ? 'E' : 'A', current, target, int(lroundf(axis.speedOutputPercent)), mode, axis.direction > 0 ? '+' : axis.direction < 0 ? '-' : '=');
      if (std::strlen(text)>16) { showBar=false; std::snprintf(text,sizeof text,"%c%s>%s",i?'E':'A',current,target); }
    }
    for (unsigned col=0; col<16; ++col) {
      uint8_t value;
      if (showBar && col==15 && !(i && runtime_.controller.model != Model::G5500)) {
        const uint8_t width=uint8_t(lroundf(axis.speedOutputPercent*5.f/100.f));
        value = width ? width : uint8_t(' ');
      } else value = col < std::strlen(text) ? text[col] : ' ';
      if(force || value!=lcdPrevious_[i][col]){lcd_->setCursor(col,i);lcd_->write(value);lcdPrevious_[i][col]=value;}
    }
  }
}
}
#endif
