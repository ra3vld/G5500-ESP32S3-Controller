// The local panel requests radio work; the network task applies it while idle. GPL-3.0.
#pragma once
namespace forkrot {
enum class PanelApResult { Idle, Pending, Applied, Unavailable, Busy, Required, NoStation, LowMemory, Failed };
struct PanelApPolicy {
  bool supported=false,busy=false,required=false,station=false,ethernet=false,memory=true;
  PanelApResult check(bool enable) const {
    if(!supported)return PanelApResult::Unavailable;
    if(busy)return PanelApResult::Busy;
    if(!enable && required)return PanelApResult::Required;
    if(!enable && !station && !ethernet)return PanelApResult::NoStation;
    if(!memory)return PanelApResult::LowMemory;
    return PanelApResult::Idle;
  }
};
inline const char *panelApMessage(PanelApResult result) {
  switch(result) {
    case PanelApResult::Pending:return "Переключение Wi-Fi";
    case PanelApResult::Unavailable:return "Wi-Fi недоступен";
    case PanelApResult::Busy:return "Дождитесь остановки";
    case PanelApResult::Required:return "ТД нужна текущему режиму";
    case PanelApResult::NoStation:return "Не настроена STA";
    case PanelApResult::LowMemory:return "Недостаточно памяти";
    case PanelApResult::Failed:return "Не удалось переключить Wi-Fi";
    default:return "ТД отключает STA";
  }
}
}
