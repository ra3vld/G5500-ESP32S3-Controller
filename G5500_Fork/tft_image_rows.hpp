// Lossless TFT image decoder: one RGB565 scanline, no heap or full framebuffer. GPL-3.0.
#pragma once
#include "tft_image.hpp"
namespace forkrot {
template<class DrawRow> bool drawTftImageRows(DrawRow drawRow) {
  uint16_t row[CAT_IMAGE_W];
  size_t at=0,pixels=0,column=0;
  auto emit=[&](uint8_t color) {
    if(color>=sizeof TFT_IMAGE_PALETTE/sizeof *TFT_IMAGE_PALETTE || pixels>=CAT_IMAGE_W*CAT_IMAGE_H) return false;
    row[column++]=TFT_IMAGE_PALETTE[color]; ++pixels;
    if(column==CAT_IMAGE_W) { drawRow(unsigned(pixels/CAT_IMAGE_W-1),row); column=0; }
    return true;
  };
  while(at<sizeof TFT_IMAGE_PACKED) {
    const uint8_t control=TFT_IMAGE_PACKED[at++];
    if(control&128) {
      if(at==sizeof TFT_IMAGE_PACKED) return false;
      const uint8_t color=TFT_IMAGE_PACKED[at++];
      for(unsigned n=0;n<(control&127u)+3;++n) if(!emit(color)) return false;
    } else {
      const unsigned count=unsigned(control)+1;
      if(count>sizeof TFT_IMAGE_PACKED-at) return false;
      for(unsigned n=0;n<count;++n) if(!emit(TFT_IMAGE_PACKED[at++])) return false;
    }
  }
  return pixels==CAT_IMAGE_W*CAT_IMAGE_H && !column;
}
}
