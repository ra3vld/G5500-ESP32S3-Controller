// Compile-time service capabilities shared by management transports. GPL-3.0.
#pragma once
#include "build_config.hpp"
#include "json_writer.hpp"
namespace forkrot {
struct Feature { const char *name; bool enabled; };
inline constexpr Feature buildFeatures[] = {
  {"display", DISPLAY_SUPPORT && DISPLAY_KIND != DISPLAY_NONE}, {"localInput", LOCAL_INPUT_SUPPORT},
  {"wifi", WIFI_SUPPORT}, {"ethernet", ETHERNET_SUPPORT}, {"web", WEB_INTERFACE_ENABLED},
  {"webPlainHtml", WEB_PLAIN_HTML_SUPPORT}, {"webLive", WEB_LIVE_SUPPORT}, {"bluetooth", BLUETOOTH_SUPPORT}, {"bluetoothSpp", BLUETOOTH_SPP_SUPPORT},
  {"gs232", GS232_SUPPORT}, {"rotctld", ROTCTLD_SUPPORT}, {"n1mm", N1MM_SUPPORT},
  {"pstrotator", PSTROTATOR_SUPPORT}, {"commandApi", COMMAND_API_SUPPORT}, {"hud", HUD_SUPPORT},
  {"hudUdp", HUD_UDP_SUPPORT}, {"hudUart", HUD_UART_SUPPORT}, {"rs485", RS485_SUPPORT},
  {"modbus", MODBUS_RTU_SUPPORT}, {"ota", OTA_SUPPORT}, {"arduinoOta", ARDUINO_OTA_SUPPORT},
  {"camera", CAMERA_ENABLED}, {"logHistory", LOG_HISTORY_SUPPORT}, {"rssiHistory", RSSI_HISTORY_SUPPORT},
  {"resetHistory", RESET_HISTORY_SUPPORT}, {"ntp", NTP_SUPPORT}, {"mqtt", MQTT_SUPPORT},
  {"homeAssistantDiscovery", HOME_ASSISTANT_DISCOVERY_SUPPORT}
};
inline void writeBuildFeatures(JsonWriter &j) {
  j << '{'; bool first=true;
  for (const auto &f:buildFeatures) { if(!first)j << ','; first=false; j << jsonQuoted(f.name) << ':' << (f.enabled?"true":"false"); }
  j << '}';
}
inline bool featureSettingEnabled(std::string_view name) {
  if(name=="n1mmPort" || name=="n1mmManualFollow")return N1MM_SUPPORT;
  if(name=="pstPort" || name=="pstManualFollow")return PSTROTATOR_SUPPORT;
  if(name=="reportPort" || name=="reportHost")return N1MM_SUPPORT || PSTROTATOR_SUPPORT;
  if(name=="gs232ManualFollow")return GS232_SUPPORT;
  if(name=="rotctldManualFollow")return ROTCTLD_SUPPORT;
  if(name=="commandPort")return COMMAND_API_SUPPORT;
  return true;
}
inline bool featurePathEnabled(std::string_view path) {
  if(path.substr(0,23)=="/api/hardware/joystick/")return LOCAL_INPUT_SUPPORT;
  if(path=="/api/mqtt")return MQTT_SUPPORT;
  if(path=="/api/rssi")return RSSI_HISTORY_SUPPORT;
  if(path=="/api/diaglog" || path=="/api/diaglog/clear")return LOG_HISTORY_SUPPORT;
  if(path.substr(0,17)=="/api/resethistory/" || path=="/api/resethistory" || path=="/api/resethistory/clear")return RESET_HISTORY_SUPPORT;
  if(path=="/api/ota/arduino")return OTA_SUPPORT && ARDUINO_OTA_SUPPORT;
  if(path.substr(0,8)=="/api/ota")return OTA_SUPPORT;
  if(path.substr(0,9)=="/api/hud/")return HUD_SUPPORT;
  if(path=="/api/wifi" || path=="/api/ap")return WIFI_SUPPORT;
  if(path=="/api/hardware/reboots")return RESET_HISTORY_SUPPORT;
  if(path=="/api/hardware/modbus")return MODBUS_RTU_SUPPORT;
  if(path=="/api/hardware/display")return DISPLAY_SUPPORT && DISPLAY_KIND!=DISPLAY_NONE;
  return true;
}
}
