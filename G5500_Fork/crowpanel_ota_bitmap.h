// Fixed monochrome scanout. Expansion uses only caller-owned internal RAM.
#pragma once
#include <stdint.h>
#include <stddef.h>
static inline void crowpanel_ota_pack(uint8_t *to,const uint16_t *from,size_t pixels) {
    for(size_t i=0;i<pixels/8;++i) {
        unsigned bits=0;
        for(unsigned bit=0;bit<8;++bit) {
            const uint16_t pixel=*from++;
            bits=(bits<<1)|!!(pixel&0x8400); // White text; retain its antialias silhouette.
        }
        to[i]=(uint8_t)bits;
    }
}
static inline __attribute__((always_inline)) void crowpanel_ota_expand(uint16_t *to,const uint8_t *from,size_t pixels) {
    for(size_t i=0;i<pixels/8;++i) {
        const unsigned bits=*from++;
        for(unsigned bit=0;bit<8;++bit)*to++=(bits&(0x80u>>bit))?0xffff:0;
    }
}
