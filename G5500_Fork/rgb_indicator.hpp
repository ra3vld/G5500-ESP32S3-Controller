// Single WS2812 indicator using the SDK's TX-only RMT driver. GPL-3.0.
// Timing and board color order match Arduino-ESP32's esp32-hal-rgb-led API.
#pragma once
#include <Arduino.h>
#if defined(ARDUINO) || defined(RGB_INDICATOR_TEST)
#include <driver/rmt_tx.h>
#include <driver/rmt_encoder.h>
#include <esp32-hal-rgb-led.h>
#endif
namespace forkrot {
class RgbIndicator {
public:
  void write(int pin,uint8_t r,uint8_t g,uint8_t b) {
#if defined(ARDUINO) || defined(RGB_INDICATOR_TEST)
    if(pin<0 || failed_ || (pin_>=0 && pin_!=pin)) return;
    if(!channel_) {
      pin_=pin;
      rmt_tx_channel_config_t config{};
      config.gpio_num=static_cast<gpio_num_t>(pin); config.clk_src=RMT_CLK_SRC_DEFAULT;
      config.resolution_hz=10000000; config.mem_block_symbols=SOC_RMT_MEM_WORDS_PER_CHANNEL; config.trans_queue_depth=1;
      rmt_copy_encoder_config_t encoderConfig{};
      if(rmt_new_tx_channel(&config,&channel_)!=ESP_OK ||
          rmt_new_copy_encoder(&encoderConfig,&encoder_)!=ESP_OK || rmt_enable(channel_)!=ESP_OK) {
        if(encoder_) rmt_del_encoder(encoder_);
        if(channel_) rmt_del_channel(channel_);
        encoder_=nullptr; channel_=nullptr; failed_=true; return;
      }
    }
    const uint32_t colors=ordered(r,g,b);
    for(unsigned i=0;i<24;++i) {
      frame_[i].level0=1; frame_[i].level1=0;
      frame_[i].duration0=(colors & (1u<<(23-i)))?8:4;
      frame_[i].duration1=12-frame_[i].duration0;
    }
    // Explicit reset/latch after every frame, including immediately adjacent flashes.
    frame_[24]={}; frame_[24].duration0=500; frame_[24].duration1=500;
    const rmt_transmit_config_t tx{};
    if(rmt_transmit(channel_,encoder_,frame_,sizeof frame_,&tx)!=ESP_OK ||
        rmt_tx_wait_all_done(channel_,20)!=ESP_OK) failed_=true;
    // The persistent frame must remain alive even if a timed-out transfer is
    // still in flight. Latch failure and leave its storage untouched until reboot.
#else
    if(pin>=0) rgbLedWrite(pin,r,g,b);
#endif
  }
private:
#if defined(ARDUINO) || defined(RGB_INDICATOR_TEST)
  static uint32_t ordered(uint8_t r,uint8_t g,uint8_t b) {
    switch(RGB_BUILTIN_LED_COLOR_ORDER) {
    case LED_COLOR_ORDER_RGB:return (uint32_t(r)<<16)|(uint32_t(g)<<8)|b;
    case LED_COLOR_ORDER_BGR:return (uint32_t(b)<<16)|(uint32_t(g)<<8)|r;
    case LED_COLOR_ORDER_BRG:return (uint32_t(b)<<16)|(uint32_t(r)<<8)|g;
    case LED_COLOR_ORDER_RBG:return (uint32_t(r)<<16)|(uint32_t(b)<<8)|g;
    case LED_COLOR_ORDER_GBR:return (uint32_t(g)<<16)|(uint32_t(b)<<8)|r;
    default:return (uint32_t(g)<<16)|(uint32_t(r)<<8)|b;
    }
  }
  rmt_channel_handle_t channel_=nullptr;
  rmt_encoder_handle_t encoder_=nullptr;
  rmt_symbol_word_t frame_[25]{};
  int pin_=-1;
  bool failed_=false;
#endif
};
}
