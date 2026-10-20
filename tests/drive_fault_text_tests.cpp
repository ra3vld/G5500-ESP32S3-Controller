// Presentation checks only: no controller, motor or firmware build.
#include "drive_fault_text.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace forkrot;
int main() {
  char status[384] = "Остановлен";
  assert(!writeDriveFault(status, sizeof status, MotionError::None, MotionError::None, ""));
  assert(std::strcmp(status, "Остановлен") == 0);
  for (auto fault : {MotionError::KickTimeout, MotionError::FeedbackStale, MotionError::FeedbackInvalid,
                     MotionError::Stalled, MotionError::Planner}) {
    assert(writeDriveFault(status, sizeof status, fault, MotionError::None, ""));
    assert(std::strstr(status, "Ошибка AZ: ") && std::strstr(status, motionFaultText(fault)));
  }
  assert(writeDriveFault(status, sizeof status, MotionError::FeedbackInvalid, MotionError::Stalled,
                        "el_stalled"));
  assert(std::strcmp(status, "Ошибка AZ: неверные показания датчика; EL: движение прекратилось") == 0);
  assert(writeDriveFault(status, sizeof status, MotionError::None, MotionError::None, "el_feedback_stale"));
  assert(std::strcmp(status, "Ошибка EL: нет свежих показаний датчика") == 0);
  assert(writeDriveFault(status, sizeof status, MotionError::None, MotionError::None, "pwm_attach_failed"));
  assert(std::strstr(status, "не удалось включить PWM"));
  assert(writeDriveFault(status, sizeof status, MotionError::Stalled, MotionError::None, "motion_storage_failed"));
  assert(std::strstr(status, "AZ: движение прекратилось") && std::strstr(status, "параметры движения не сохранены"));
  assert(writeDriveFault(status, sizeof status, MotionError::None, MotionError::None, "", false, true));
  assert(std::strstr(status, "AZ: неверные параметры движения"));
  assert(writeDriveFault(status, sizeof status, MotionError::None, MotionError::None, "future_fault"));
  assert(std::strstr(status, "future_fault"));
  assert(writeDriveFault(status, sizeof status, MotionError::None, MotionError::None, "pin_configuration_invalid"));
  assert(std::strstr(status, "распиновка GPIO"));
  char tiny[8];
  assert(writeDriveFault(tiny, sizeof tiny, MotionError::Stalled, MotionError::Planner, ""));
  assert(tiny[sizeof tiny - 1] == 0);
  std::cout << "PASS: drive fault text, both axes, legacy HUD, common faults, normal notices, bounds\n";
}
