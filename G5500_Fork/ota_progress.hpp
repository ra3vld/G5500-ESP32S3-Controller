// OTA presentation state; independent of transport and display hardware.
#pragma once
#include <cstdint>
namespace forkrot {
enum class OtaStage : uint8_t { Idle, Preparing, Writing, Verifying, Complete, Error };
struct OtaProgress {
  OtaStage stage=OtaStage::Idle;
  uint8_t percent=0;
};
inline uint8_t otaPercent(uint32_t received,uint32_t total,bool complete=false) {
  if(complete)return 100;
  if(!total)return 0;
  const auto value=uint64_t(received)*100/total;
  return uint8_t(value>99?99:value);
}
inline const char *otaStageText(OtaStage stage) {
  switch(stage) {
  case OtaStage::Preparing:return "Идет подготовка OTA";
  case OtaStage::Writing:return "Идет запись OTA";
  case OtaStage::Verifying:return "Идет проверка OTA";
  case OtaStage::Complete:return "Идет перезагрузка";
  case OtaStage::Error:return "Ошибка OTA";
  default:return "";
  }
}
}
