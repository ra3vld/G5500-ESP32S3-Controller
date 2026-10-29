#pragma once
#include <Arduino.h>
enum { ADC_UNIT_1, ADC_ATTEN_DB_12=3, ADC_BITWIDTH_12=12 };
struct adc_cali_line_fitting_config_t {
  int unit_id,atten,bitwidth;
  uint32_t default_vref;
};
struct FakeAdcCalibration { adc_cali_line_fitting_config_t config; };
using adc_cali_handle_t=FakeAdcCalibration *;
struct FakeAdcState {
  unsigned creates=0,conversions=0,live=0;
  bool createOk=true,convertOk=true;
  adc_cali_line_fitting_config_t config{};
};
inline FakeAdcState fakeAdc;
inline int adc_cali_create_scheme_line_fitting(const adc_cali_line_fitting_config_t *config,adc_cali_handle_t *handle) {
  ++fakeAdc.creates;fakeAdc.config=*config;
  // Reproduce a chip without eFuse calibration: zero default_vref is invalid.
  if(!fakeAdc.createOk || !config->default_vref)return -1;
  *handle=new FakeAdcCalibration{*config};++fakeAdc.live;return 0;
}
inline int adc_cali_raw_to_voltage(adc_cali_handle_t handle,int raw,int *millivolts) {
  ++fakeAdc.conversions;++fakeHardware.readsMv;
  if(!handle || !fakeAdc.convertOk)return -1;
  *millivolts=raw;return 0;
}
inline int adc_cali_delete_scheme_line_fitting(adc_cali_handle_t handle) {
  delete handle;--fakeAdc.live;return 0;
}
