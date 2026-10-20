#pragma once
#include "rmt_tx.h"
struct rmt_copy_encoder_config_t {};
inline int rmt_new_copy_encoder(const rmt_copy_encoder_config_t *,rmt_encoder_handle_t *out) {
  ++fakeRmt.encoders;if(!fakeRmt.encoderResult)*out=&fakeRmt.encoders;return fakeRmt.encoderResult;
}
inline int rmt_del_encoder(rmt_encoder_handle_t) {++fakeRmt.encoderDeletes;return 0;}
