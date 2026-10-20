#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
using gpio_num_t=int;
using rmt_channel_handle_t=void *;
using rmt_encoder_handle_t=void *;
inline constexpr int ESP_OK=0,RMT_CLK_SRC_DEFAULT=0,SOC_RMT_MEM_WORDS_PER_CHANNEL=64;
struct rmt_symbol_word_t { uint32_t duration0=0,level0=0,duration1=0,level1=0; };
struct rmt_tx_channel_config_t { int gpio_num=0,clk_src=0;uint32_t resolution_hz=0;size_t mem_block_symbols=0,trans_queue_depth=0; };
struct rmt_transmit_config_t {};
struct FakeRmt {
  int createResult=0,encoderResult=0,enableResult=0,txResult=0,waitResult=0;
  unsigned creates=0,encoders=0,enables=0,sends=0,channelDeletes=0,encoderDeletes=0;
  int waitMs=0;
  const void *payload=nullptr;
  rmt_tx_channel_config_t config;
  std::vector<rmt_symbol_word_t> sent;
};
inline FakeRmt fakeRmt;
inline int rmt_new_tx_channel(const rmt_tx_channel_config_t *c,rmt_channel_handle_t *out) {
  ++fakeRmt.creates;fakeRmt.config=*c;
  if(!fakeRmt.createResult)*out=&fakeRmt;
  return fakeRmt.createResult;
}
inline int rmt_enable(rmt_channel_handle_t) {++fakeRmt.enables;return fakeRmt.enableResult;}
inline int rmt_del_channel(rmt_channel_handle_t) {++fakeRmt.channelDeletes;return 0;}
inline int rmt_transmit(rmt_channel_handle_t,rmt_encoder_handle_t,const void *p,size_t bytes,const rmt_transmit_config_t *) {
  ++fakeRmt.sends;fakeRmt.payload=p;
  const auto *symbols=static_cast<const rmt_symbol_word_t *>(p);
  fakeRmt.sent.assign(symbols,symbols+bytes/sizeof(*symbols));return fakeRmt.txResult;
}
inline int rmt_tx_wait_all_done(rmt_channel_handle_t,int ms) {fakeRmt.waitMs=ms;return fakeRmt.waitResult;}
