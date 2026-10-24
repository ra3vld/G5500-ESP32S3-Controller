// Desktop presentation of recorded input. Does not alter the firmware display.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>

namespace traceviz {
inline constexpr unsigned Size = 600, DisplaySize = 480, Margin = (Size - DisplaySize) / 2;
using Image = std::array<uint8_t, Size * Size * 3>;
struct Point { double x, y; };
struct Color { double r, g, b; };

class Inputs {
  struct Stroke {
    std::deque<Point> points;
    uint64_t started = 0, ended = 0;
    bool down = true, cancelled = false;
  };
  std::deque<Stroke> strokes_;
  bool touchDown_ = false;
  double angleFrom_ = 0, angleTarget_ = 0, pressFrom_ = 0, pressTarget_ = 0;
  uint64_t turnedAt_ = 0, pressedAt_ = 0;
  bool turned_ = false, cached_ = false;
  Image base_{}, depressed_{}, image_{};
  static constexpr double Pi = 3.14159265358979323846;
  static double ease(uint64_t now, uint64_t at, double duration) {
    const double fraction = std::clamp(double(now >= at ? now - at : 0) / duration, 0., 1.);
    return 1 - std::pow(1 - fraction, 3);
  }
  double angle(uint64_t now) const { return angleFrom_ + (angleTarget_ - angleFrom_) * ease(now, turnedAt_, 120); }
  double pressure(uint64_t now) const { return pressFrom_ + (pressTarget_ - pressFrom_) * ease(now, pressedAt_, 75); }
  static double coverage(double distance, double radius) { return std::clamp(radius + .5 - distance, 0., 1.); }
  void pixel(int x, int y, Color color, double alpha, bool clip = false) {
    if (x < 0 || y < 0 || x >= int(Size) || y >= int(Size)) return;
    if (clip && std::hypot(x + .5 - Size / 2., y + .5 - Size / 2.) > DisplaySize / 2.) return;
    alpha = std::clamp(alpha, 0., 1.);
    auto *p = image_.data() + (y * Size + x) * 3;
    p[0] = uint8_t(std::lround(p[0] * (1 - alpha) + color.r * alpha));
    p[1] = uint8_t(std::lround(p[1] * (1 - alpha) + color.g * alpha));
    p[2] = uint8_t(std::lround(p[2] * (1 - alpha) + color.b * alpha));
  }
  void circle(Point center, double radius, double width, Color color, double alpha, bool clip = false) {
    const double outer = radius + width / 2, inner = std::max(0., radius - width / 2);
    for (int y = int(std::floor(center.y - outer - 1)); y <= int(std::ceil(center.y + outer + 1)); ++y)
      for (int x = int(std::floor(center.x - outer - 1)); x <= int(std::ceil(center.x + outer + 1)); ++x) {
        const double d = std::hypot(x + .5 - center.x, y + .5 - center.y);
        const double a = coverage(d, outer) * (inner ? 1 - coverage(d, inner) : 1);
        if (a > 0) pixel(x, y, color, alpha * a, clip);
      }
  }
  void line(Point from, Point to, double width, Color color, double alpha, bool clip = false) {
    const double dx = to.x - from.x, dy = to.y - from.y, squared = dx * dx + dy * dy;
    const double radius = width / 2;
    for (int y = int(std::floor(std::min(from.y, to.y) - radius - 1));
         y <= int(std::ceil(std::max(from.y, to.y) + radius + 1)); ++y)
      for (int x = int(std::floor(std::min(from.x, to.x) - radius - 1));
           x <= int(std::ceil(std::max(from.x, to.x) + radius + 1)); ++x) {
        const double part = squared ? std::clamp(((x + .5 - from.x) * dx + (y + .5 - from.y) * dy) / squared, 0., 1.) : 0;
        const double d = std::hypot(x + .5 - from.x - part * dx, y + .5 - from.y - part * dy);
        const double a = coverage(d, radius);
        if (a > 0) pixel(x, y, color, alpha * a, clip);
      }
  }
  static Point onRing(double theta, double radius, double push) {
    return {Size / 2. + std::sin(theta) * radius, Size / 2. - std::cos(theta) * radius + push * 2};
  }
  void cacheRing() {
    if (cached_) return;
    for (unsigned pressed = 0; pressed < 2; ++pressed) {
      auto &surface = pressed ? depressed_ : base_;
      for (unsigned y = 0; y < Size; ++y) for (unsigned x = 0; x < Size; ++x) {
        const double dx = x + .5 - Size / 2., dy = y + .5 - Size / 2. - 2 * pressed;
        const double r = std::hypot(dx, dy), theta = std::atan2(dy, dx);
        double red = 12, green = 18, blue = 26;
        if (r > 241 && r < 291) {
          const double shadow = coverage(r, 291) * (1 - coverage(r, 241));
          red *= 1 - .75 * shadow; green *= 1 - .75 * shadow; blue *= 1 - .75 * shadow;
        }
        if (r > 245 && r < 285) {
          double light = 63 + 18 * std::cos(theta + .7) + 12 * std::cos(2 * theta - .3);
          light += 11 * std::exp(-std::pow((r - 282) / 1.3, 2));
          light -= 23 * std::exp(-std::pow((r - 248) / 2.4, 2));
          light += (int(r * 2.5) % 2 ? 1.6 : -1.6);
          light *= pressed ? .63 : 1;
          red = light * .87; green = light * .96; blue = light * 1.08;
        }
        const auto index = (y * Size + x) * 3;
        surface[index] = uint8_t(std::clamp(red, 0., 255.));
        surface[index + 1] = uint8_t(std::clamp(green, 0., 255.));
        surface[index + 2] = uint8_t(std::clamp(blue, 0., 255.));
      }
    }
    cached_ = true;
  }
public:
  void reset() {
    strokes_.clear(); touchDown_ = false; turned_ = false;
    angleFrom_ = angleTarget_ = pressFrom_ = pressTarget_ = 0;
    turnedAt_ = pressedAt_ = 0;
  }
  void clear(uint64_t now) {
    // A gap ends ownership, while already observed path segments may fade.
    // Known encoder steps still finish; missing records cannot cancel them.
    if (touchDown_ && !strokes_.empty()) {
      strokes_.back().down = false; strokes_.back().ended = now; strokes_.back().cancelled = true;
    }
    touchDown_ = false;
    pressFrom_ = pressure(now); pressTarget_ = 0; pressedAt_ = now;
  }
  void touch(uint64_t now, bool healthy, bool down, unsigned x, unsigned y) {
    down = healthy && down && x < DisplaySize && y < DisplaySize;
    if (down) {
      if (!touchDown_) {
        if (strokes_.size() >= 8) strokes_.pop_front();
        strokes_.push_back({{}, now, 0, true});
      }
      auto &stroke = strokes_.back();
      const Point p{double(x + Margin), double(y + Margin)};
      if (stroke.points.empty() || std::hypot(p.x - stroke.points.back().x, p.y - stroke.points.back().y) >= 1) {
        if (stroke.points.size() >= 192) stroke.points.erase(stroke.points.begin() + 1);
        stroke.points.push_back(p);
      }
    } else if (touchDown_ && !strokes_.empty()) {
      strokes_.back().down = false; strokes_.back().ended = now; strokes_.back().cancelled = !healthy;
    }
    touchDown_ = down;
  }
  void knob(uint64_t now, bool healthy, bool down, int delta) {
    const double nextPress = healthy && down ? 1 : 0;
    if (pressTarget_ != nextPress) {
      pressFrom_ = pressure(now); pressTarget_ = nextPress; pressedAt_ = now;
    }
    if (healthy && delta) {
      angleFrom_ = angle(now); angleTarget_ += delta * (Pi / 12); turnedAt_ = now; turned_ = true;
    }
  }
  const Image &render(const uint8_t *display, uint64_t now) {
    cacheRing();
    const double push = pressure(now);
    for (size_t i = 0; i < image_.size(); ++i)
      image_[i] = uint8_t(std::lround(base_[i] * (1 - push) + depressed_[i] * push));
    for (unsigned y = 0; y < DisplaySize; ++y) for (unsigned x = 0; x < DisplaySize; ++x) {
      if (std::hypot(x + .5 - DisplaySize / 2., y + .5 - DisplaySize / 2.) > DisplaySize / 2.) continue;
      const auto src = (y * DisplaySize + x) * 3, dst = ((y + Margin) * Size + x + Margin) * 3;
      std::copy_n(display + src, 3, image_.data() + dst);
    }
    const double rotation = angle(now);
    for (unsigned tick = 0; tick < 60; ++tick) {
      const double theta = rotation + tick * Pi / 30;
      line(onRing(theta, tick % 5 ? 271 : 265, push), onRing(theta, 279 - push, push),
           tick % 5 ? 1.5 : 2.1, {13, 20, 29}, .8);
      line(onRing(theta + .002, tick % 5 ? 271 : 265, push), onRing(theta + .002, 278 - push, push),
           .6, {150, 169, 184}, .4 * (1 - push * .4));
    }
    line(onRing(rotation, 252, push), onRing(rotation, 280 - push, push), 3.2,
         {106, 218, 255}, .85);
    circle(onRing(rotation, 281 - push, push), 0, 5.6, {196, 244, 255}, .9);
    circle({Size / 2., Size / 2. + push * 2}, 245, 2.3, {90, 202, 244}, .1 + push * .7);
    if (turned_ && now - turnedAt_ < 350)
      circle({Size / 2., Size / 2. + push * 2}, 287, 1.8, {106, 218, 255}, .5 * (1 - double(now - turnedAt_) / 350));
    constexpr Color amber{255, 195, 95}, white{255, 246, 226};
    for (const auto &stroke : strokes_) {
      if (stroke.points.empty()) continue;
      const double age = double(now >= stroke.ended ? now - stroke.ended : 0);
      const double alpha = stroke.down ? 1 : std::clamp(1 - age / 650, 0., 1.);
      if (alpha <= 0) continue;
      const auto first = stroke.points.front(), last = stroke.points.back();
      for (size_t i = 1; i < stroke.points.size(); ++i)
        line(stroke.points[i - 1], stroke.points[i], 3.5, amber, .72 * alpha, true);
      if (stroke.cancelled) continue; // A missing sample is not a recorded tap/release.
      if (stroke.points.size() > 1 && std::hypot(last.x - first.x, last.y - first.y) > 10) {
        const auto prev = stroke.points[stroke.points.size() - 2];
        const double theta = std::atan2(last.y - prev.y, last.x - prev.x);
        for (const double side : {-1., 1.})
          line(last, {last.x - 14 * std::cos(theta + side * .55), last.y - 14 * std::sin(theta + side * .55)},
               3.5, amber, .85 * alpha, true);
        circle(first, 0, 6, white, .75 * alpha, true);
      }
      circle(last, 13, 2.4, amber, .95 * alpha, true);
      circle(last, 0, 19, amber, .14 * alpha, true);
      circle(last, 0, 5, white, .9 * alpha, true);
      if (!stroke.down) circle(last, 13 + age / 650 * 25, 1.6, amber, .65 * alpha, true);
    }
    return image_;
  }
};
}
