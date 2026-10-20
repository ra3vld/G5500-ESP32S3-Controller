// Drive fault presentation shared with native checks. GPL-3.0.
#pragma once
#include "motion.hpp"
#include <cstdio>
#include <cstring>
namespace forkrot {
inline const char *motionFaultText(MotionError error) {
  switch (error) {
  case MotionError::None: return "";
  case MotionError::KickTimeout: return "привод не тронулся";
  case MotionError::FeedbackStale: return "нет свежих показаний датчика";
  case MotionError::FeedbackInvalid: return "неверные показания датчика";
  case MotionError::Stalled: return "движение прекратилось";
  case MotionError::Planner: return "ошибка профиля движения";
  }
  return "неизвестная ошибка привода";
}
inline const char *driveFaultText(const char *code) {
  struct Entry { const char *code, *text; };
  static constexpr Entry entries[] = {
    {"kick_start_timeout", "привод не тронулся"},
    {"feedback_stale", "нет свежих показаний датчика"},
    {"feedback_invalid", "неверные показания датчика"},
    {"stalled", "движение прекратилось"},
    {"planner_error", "ошибка профиля движения"},
    {"calibration_storage_invalid", "ошибка сохранённой калибровки"},
    {"calibration_save_failed", "калибровка не сохранена"},
    {"target_not_calibrated", "нужна калибровка"},
    {"sequence_calibration", "потеря калибровки в последовательности"},
    {"continuous_motion_timeout", "превышено время вращения"},
    {"udp_bind_failed", "не удалось открыть UDP-порт"},
    {"pwm_attach_failed", "не удалось включить PWM"},
    {"motion_storage_failed", "параметры движения не сохранены"},
    {"crowpanel_no_drive", "привод не подключён"},
    {"pin_configuration_invalid", "неполная или конфликтующая распиновка GPIO"},
    {"crowpanel_drive_failed", "ошибка выходов привода CrowPanel"},
    {"hud_offline", "нет связи с повороткой"},
  };
  for (const auto &entry : entries) if (std::strcmp(code, entry.code) == 0) return entry.text;
  return code;
}
inline bool writeDriveFault(char *out, size_t size, MotionError az, MotionError el,
                            const char *fault, bool azReady = true, bool elReady = true) {
  const char *a = az != MotionError::None ? motionFaultText(az) : azReady ? "" : "неверные параметры движения";
  const char *e = el != MotionError::None ? motionFaultText(el) : elReady ? "" : "неверные параметры движения";
  // Older HUD peers may provide only the common fault string.
  const bool azFault = std::strncmp(fault, "az_", 3) == 0;
  const bool elFault = std::strncmp(fault, "el_", 3) == 0;
  if (azFault && !*a) a = driveFaultText(fault + 3);
  if (elFault && !*e) e = driveFaultText(fault + 3);
  const char *general = *fault && !azFault && !elFault ? driveFaultText(fault) : "";
  if (!*a && !*e && !*general) return false;
  std::snprintf(out, size, "Ошибка%s%s%s%s%s%s%s",
                *a ? " AZ: " : "", a, *a && *e ? "; " : "",
                *e ? (*a ? "EL: " : " EL: ") : "", e,
                *general ? (*a || *e ? "; " : ": ") : "", general);
  return true;
}
} // namespace forkrot
