// Persistent ADC calibration; no allocation in the sampling loop. GPL-3.0.
#pragma once
#include "build_config.hpp"
#include <Arduino.h>
#if FORK_CLASSIC_ESP32
#include <esp_adc/adc_cali_scheme.h>
#endif
namespace forkrot {
class AdcMillivolts {
#if FORK_CLASSIC_ESP32
  // Every feedback ADC allowed by the classic board profiles is on ADC1,
  // configured by Hardware::beginAdc for 12 bits and 11/12 dB attenuation.
  adc_cali_handle_t calibration_=nullptr;
  bool attempted_=false;
#endif
public:
  AdcMillivolts()=default;
  AdcMillivolts(const AdcMillivolts &)=delete;
  AdcMillivolts &operator=(const AdcMillivolts &)=delete;
  ~AdcMillivolts() {
#if FORK_CLASSIC_ESP32
    if(calibration_)adc_cali_delete_scheme_line_fitting(calibration_);
#endif
  }
  bool begin() {
#if FORK_CLASSIC_ESP32
    if(!attempted_) {
      attempted_=true;
      adc_cali_line_fitting_config_t config{};
      config.unit_id=ADC_UNIT_1;config.atten=ADC_ATTEN_DB_12;config.bitwidth=ADC_BITWIDTH_12;
      // eFuse calibration takes precedence. Early ESP32 chips require a
      // nominal reference when factory calibration was not burnt into eFuse.
      config.default_vref=1100;
      if(adc_cali_create_scheme_line_fitting(&config,&calibration_)!=0)calibration_=nullptr;
    }
    return calibration_!=nullptr;
#else
    return true;
#endif
  }
  int read(int pin) {
#if FORK_CLASSIC_ESP32
    if(!begin())return -1;
    int millivolts=0;
    if(adc_cali_raw_to_voltage(calibration_,analogRead(pin),&millivolts)!=0)return -1;
    return millivolts;
#else
    return analogReadMilliVolts(pin);
#endif
  }
};
}
