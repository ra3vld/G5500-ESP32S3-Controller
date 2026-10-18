#include "motion_fixture.hpp"
#include "crowpanel_knob.hpp"
#include "quadrature.hpp"
#include "crowpanel_touch.hpp"
#include "crowpanel_dial.hpp"
#include <cassert>
#include <cstdio>
using namespace forkrot;
int main() {
  QuadratureDecoder decoder(2);
  decoder.reset(3);
  assert(decoder.update(1) == 0);
  assert(decoder.update(0) == 1);
  assert(decoder.update(2) == 0);
  assert(decoder.update(3) == 1);
  decoder.reset(3);
  assert(decoder.update(1) == 0);
  assert(decoder.update(3) == 0); // bounce cancels
  assert(decoder.update(0) == 0); // invalid diagonal resets
  assert(decoder.update(2) == 0);
  assert(decoder.update(3) == 1);
  uint16_t x = 0, y = 0;
  uint8_t touch[5] = {1, 0, 239, 1, 223};
  assert(crowpanel::decodeTouch(touch, x, y) && x == 239 && y == 479);
  touch[4] = 224; assert(!crowpanel::decodeTouch(touch, x, y));
  touch[4] = 10; touch[1] = 0x40; assert(!crowpanel::decodeTouch(touch, x, y));
  touch[1] = 0; touch[0] = 2; assert(!crowpanel::decodeTouch(touch, x, y));
  crowpanel::TouchButtons buttons;
  using crowpanel::TouchAction;
  assert(buttons.sample(true, 240, 106) == TouchAction::Mode);
  assert(buttons.pressed() == TouchAction::Mode);
  for (int i = 0; i < 100; ++i) assert(buttons.sample(true, 240, 106) == TouchAction::None);
  buttons.sample(false, 0, 0);
  assert(buttons.pressed() == TouchAction::None);
  assert(buttons.sample(true, 240, 106) == TouchAction::Mode);
  assert(buttons.sample(true, 240, 320) == TouchAction::None); // Sliding out cancels the button.
  assert(buttons.pressed() == TouchAction::None);
  buttons.sample(false, 0, 0);
  assert(buttons.sample(true, 240, 320) == TouchAction::None); // Removed STOP area is inert.
  buttons.sample(false, 0, 0);
  assert(buttons.sample(true, 139, 106) == TouchAction::Target);
  assert(buttons.sample(true, 240, 106) == TouchAction::None); // Must start inside button.
  buttons.sample(false, 0, 0);
  // Broad annulus, compass bearings, one command per contact, no slide-in commands.
  assert(crowpanel::dialContains(380, 240) && crowpanel::dialContains(479, 240));
  assert(!crowpanel::dialContains(379, 240) && !crowpanel::dialContains(0, 0));
  assert(!crowpanel::dialContains(65535, 65535));
  assert(crowpanel::dialBearing(240, 13) == 0 && crowpanel::dialBearing(467, 240) == 90);
  assert(crowpanel::dialBearing(240, 467) == 180 && crowpanel::dialBearing(13, 240) == 270);
  assert(crowpanel::dialBearing(400, 80) == 45 && crowpanel::dialBearing(80, 80) == 315);
  assert(buttons.sample(true, 467, 240) == TouchAction::Target);
  assert(buttons.sample(true, 240, 467) == TouchAction::None);
  buttons.sample(false, 0, 0);
  assert(buttons.sample(true, 240, 240) == TouchAction::None);
  assert(buttons.sample(true, 467, 240) == TouchAction::None);
  buttons.sample(false, 0, 0);
  assert(buttons.sample(true, 467, 240, false) == TouchAction::None); // HUD selector owns taps.
  buttons.sample(false, 0, 0);
  // The displayed confirmation owns overlapping ring taps and fires once.
  assert(buttons.sample(true, 400, 280, true, true) == TouchAction::Confirm);
  assert(buttons.sample(true, 400, 280, true, true) == TouchAction::None);
  assert(buttons.sample(true, 400, 280, true, false) == TouchAction::None);
  buttons.sample(false, 0, 0);
  assert(buttons.sample(true, 400, 280, false, true) == TouchAction::None);
  buttons.sample(false, 0, 0);
  assert(buttons.sample(true, 240, 90, true, true) == TouchAction::Mode);
  assert(crowpanel::limitOpacity(120) == 0 && crowpanel::limitOpacity(121) == 0);
  assert(crowpanel::limitOpacity(90) > 0 && crowpanel::limitOpacity(90) < crowpanel::limitOpacity(20));
  assert(crowpanel::limitOpacity(0) == 255 && crowpanel::limitOpacity(-1) == 255);
  assert(crowpanel::limitOpacity(450 - 350) > 0 && crowpanel::limitOpacity(450 - 90) == 0);
  assert(crowpanel::limitOpacity(NAN) == 0);
  assert(crowpanel::shortestBearingDelta(359, 1) == 2);
  assert(crowpanel::shortestBearingDelta(1, 359) == -2);
  Controller c = forkrot::testController();
  c.feedback(100, 20, true, true);
  CrowPanelKnob knob(c);
  uint32_t now = 50;
  auto sample = [&](bool down, int delta = 0, uint32_t advance = 40, bool healthy = true) {
    now += advance; knob.sample(healthy, down, delta, now, false);
  };
  auto press = [&] { sample(true); sample(true); };
  auto release = [&] { sample(false); sample(false); };
  sample(false); sample(false, 1);
  assert(c.mode == Mode::Manual && c.axes[0].target == 120 && c.axes[0].tracking);
  c.tick(now); assert(c.moving());
  c.feedback(105, 20, true, true);
  sample(false, 2); assert(c.axes[0].target == 160 && c.moving());
  sample(false, -1); assert(c.axes[0].target == 140 && c.moving());
  press(); assert(c.axes[0].target == 140 && !c.moving()); // First press after rotation pauses.
  release(); sample(false, 0, 351); assert(!c.moving());
  assert(!c.axes[0].tracking); // stop press cannot restart on release
  press(); release(); sample(false, 0, 351);
  assert(c.axes[0].target == 140 && c.axes[0].tracking); // Second click resumes the same target.
  c.tick(now); assert(c.moving());
  press(); release(); sample(false, 0, 351); assert(!c.moving());
  sample(false, 1); assert(c.axes[0].target == 125 && c.axes[0].tracking); // After STOP start at current.
  knob.stop(now);
  press(); release(); press(); release();
  assert(knob.axis() == 1 && !c.moving());
  sample(false, -1000); assert(c.axes[1].target == 0 && c.axes[1].tracking);
  sample(false, 1000); assert(c.axes[1].target == 180);
  press(); sample(true, 0, 1201); release(); sample(false, 0, 351);
  assert(c.mode == Mode::Auto && knob.modeChanged && !c.moving());
  knob.modeChanged = false;
  // Touch uses the same toggle, cancels a pending click and never starts movement.
  sample(false, 1); press(); release();
  knob.toggleMode(now);
  sample(false, 0, 351);
  assert(c.mode == Mode::Manual && knob.modeChanged && !c.moving());
  c.setMode(Mode::AutoHour, now);
  knob.toggleMode(now);
  assert(c.mode == Mode::Manual);
  knob.modeChanged = false;
  // Loss of I2C during a press must not turn its later release into confirm.
  sample(false, 10); press(); sample(false, 0, 40, false);
  sample(true, 100); release(); sample(false, 0, 400);
  assert(!c.moving() && !c.axes[1].tracking);
  sample(false, 1); assert(c.axes[1].tracking);
  c.stop(); sample(false); assert(!c.axes[1].tracking);
  // A pending external target still requires a short click in manual mode.
  assert(c.target({200, 0, true, false}, true, now) == Result::Pending);
  press(); release(); sample(false, 0, 351);
  assert(c.axes[0].target == 200 && c.axes[0].tracking && !c.pendingValid);
  // Touch confirmation executes a pending goal even during motion; it is not STOP.
  if (knob.axis()) knob.selectAxis();
  c.tick(now);
  assert(c.moving());
  assert(c.target({240, 0, true, false}, true, now) == Result::Pending);
  knob.confirmPending(now + 1);
  assert(c.axes[0].target == 240 && c.axes[0].tracking && !c.pendingValid);
  knob.confirmPending(now + 2);
  assert(c.axes[0].target == 240 && c.axes[0].tracking);
  knob.stop(now);
  c.setModel(Model::G800DXA); sample(false);
  assert(knob.axis() == 0);
  c.feedback(105, 20, true, true);
  c.maximum = 360; sample(false, 1000); assert(c.axes[0].target == 360);
  knob.stop(now);
  // A fault rejects encoder targets without starting the drive.
  c.driveFault = true;
  sample(false, -1); assert(knob.lastResult == Result::Invalid && !c.axes[0].tracking && !c.moving());
  // Stable release is mandatory when booting with the button held.
  CrowPanelKnob held(c);
  held.sample(true, true, 30, 100);
  held.sample(true, true, 30, 2000);
  assert(!c.axes[0].tracking && !held.modeChanged);
  Controller inverted = forkrot::testController();
  inverted.feedback(100, 20, true, true);
  CrowPanelKnob reverse(inverted);
  reverse.sample(true, false, 1, 100); // Inversion enabled by default.
  assert(inverted.axes[0].target == 80);
  reverse.sample(true, false, 1, 140); assert(inverted.axes[0].target == 60);
  inverted.tick(140); assert(inverted.moving());
  reverse.sample(true, false, 0, 180, false);
  reverse.confirm(180); assert(!inverted.moving()); // First press stops even immediately after rotation.
  reverse.stop(180);
  reverse.sample(true, false, 1, 220, false); assert(inverted.axes[0].target == 120);
  // A remote replacement must not be undone by an old local confirmation.
  inverted.target({200, 0, true, false}, false, 220);
  inverted.tick(220); assert(inverted.moving());
  reverse.sample(true, true, 0, 250, false);
  reverse.sample(true, true, 0, 290, false);
  assert(!inverted.moving() && inverted.axes[0].target == 200);
  // After arrival, a press executes the pending external target in manual mode.
  Controller arrived = forkrot::testController();
  arrived.feedback(100, 20, true, true);
  CrowPanelKnob completed(arrived);
  completed.sample(true, false, -1, 100);
  arrived.feedback(120, 20, true, true); arrived.tick(200);
  assert(arrived.axes[0].completed);
  assert(arrived.target({200, 0, true, false}, true, 220) == Result::Pending);
  completed.sample(true, true, 0, 250); completed.sample(true, true, 0, 290);
  completed.sample(true, false, 0, 330); completed.sample(true, false, 0, 370);
  completed.sample(true, false, 0, 730);
  assert(arrived.axes[0].target == 200 && arrived.axes[0].tracking && !arrived.pendingValid);
  // Pause both unfinished axes, resume on a single click, keep double/long gestures safe.
  Controller paused = forkrot::testController();
  paused.feedback(10, 20, true, true);
  CrowPanelKnob pauseKnob(paused);
  uint32_t at = 100;
  auto pauseSample = [&](bool down, uint32_t elapsed = 40) {
    at += elapsed; pauseKnob.sample(true, down, 0, at); paused.tick(at);
  };
  auto pausePress = [&] { pauseSample(true); pauseSample(true); };
  auto pauseRelease = [&] { pauseSample(false); pauseSample(false); };
  pauseSample(false);
  paused.target({100, 80, true, true}, false, at); paused.tick(at);
  pausePress(); assert(!paused.moving()); pauseRelease(); pauseSample(false, 351);
  assert(!paused.moving());
  pausePress(); pauseRelease(); pauseSample(false, 351);
  assert(paused.axes[0].tracking && paused.axes[1].tracking && paused.moving());
  assert(paused.axes[0].target == 100 && paused.axes[1].target == 80);
  pausePress(); pauseRelease(); pauseSample(false, 351);
  pausePress(); pauseSample(true, 1201); pauseRelease(); pauseSample(false, 351);
  assert(!paused.moving() && paused.mode == Mode::Auto); // Hold changes mode, never resumes.
  pausePress(); pauseRelease(); pauseSample(false, 351); assert(!paused.moving());
  paused.target({100, 80, true, true}, false, at); paused.tick(at);
  pausePress(); pauseRelease(); pauseSample(false, 351);
  pausePress(); pauseRelease(); pausePress(); pauseRelease(); pauseSample(false, 351);
  assert(pauseKnob.axis() == 1 && !paused.moving()); // Double-click selects axis, cancels resume.
  paused.target({100, 80, true, true}, false, at); paused.tick(at);
  pausePress(); pauseRelease(); pauseSample(false, 351);
  paused.stop(); // External STOP invalidates a knob pause.
  pausePress(); pauseRelease(); pauseSample(false, 351); assert(!paused.moving());
  // A quick click immediately after a detent may produce only one pressed sample.
  // STOP must win over accumulated rotation; bounce/release must not resume it.
  Controller quick = forkrot::testController();
  quick.feedback(100, 20, true, true);
  CrowPanelKnob quickKnob(quick);
  quickKnob.sample(true, false, -1, 100);
  quick.tick(100);
  assert(quick.moving() && quick.axes[0].target == 120);
  quickKnob.sample(true, true, -1, 105);
  assert(!quick.moving() && quick.axes[0].target == 120); // STOP on the first read.
  quickKnob.sample(true, false, 0, 115);
  quickKnob.sample(true, true, 0, 120); // Contact bounce, not a second click.
  quickKnob.sample(true, false, -1, 125);
  quickKnob.sample(true, false, -1, 155); // Discard release-time encoder noise too.
  quickKnob.sample(true, false, 0, 510);
  quick.tick(510);
  assert(!quick.moving() && quick.axes[0].target == 120);
  // One new short click after stable release resumes the original target.
  quickKnob.sample(true, true, 0, 520);
  quickKnob.sample(true, false, 0, 530);
  quickKnob.sample(true, false, 0, 560);
  quickKnob.sample(true, false, 0, 911);
  quick.tick(911);
  assert(quick.moving() && quick.axes[0].target == 120);
  Controller tapped = forkrot::testController();
  tapped.feedback(400, 20, true, true);
  CrowPanelKnob touchKnob(tapped);
  touchKnob.setBearing(90, 100);
  assert(tapped.axes[0].target == 450 && tapped.axes[0].tracking && !tapped.pendingValid);
  touchKnob.setBearing(270, 110);
  assert(tapped.axes[0].target == 270);
  tapped.maximum = 360;
  tapped.feedback(350, 20, true, true);
  touchKnob.setBearing(0, 120);
  assert(tapped.axes[0].target == 360); // Nearest reachable turn, not a full revolution.
  touchKnob.selectAxis();
  touchKnob.setBearing(90, 130);
  assert(tapped.axes[1].target == 90 && tapped.axes[0].target == 360);
  touchKnob.setBearing(270, 140);
  assert(touchKnob.lastResult == Result::Invalid && tapped.axes[1].target == 90);
  // A queued knob click must not stop the newly tapped target.
  Controller clickThenTap = forkrot::testController();
  clickThenTap.feedback(20, 30, true, true);
  CrowPanelKnob pendingClick(clickThenTap);
  pendingClick.sample(true, false, 0, 100);
  pendingClick.sample(true, true, 0, 110);
  pendingClick.sample(true, false, 0, 120);
  pendingClick.sample(true, false, 0, 160);
  pendingClick.setBearing(90, 170);
  pendingClick.sample(true, false, 0, 600);
  clickThenTap.tick(600);
  assert(clickThenTap.axes[0].target == 90 && clickThenTap.moving());
  std::puts("PASS: CrowPanel ring targets, gestures, pause/resume, inversion, touch and faults");
}
