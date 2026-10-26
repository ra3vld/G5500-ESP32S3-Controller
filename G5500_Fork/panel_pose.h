// Shared display/touch geometry; also used by the RGB refill interrupt. GPL-3.0.
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef struct { int x, y; } panel_point_t;
#define PANEL_INLINE static inline __attribute__((always_inline))
// Pose: clockwise quarter turns, then mirrors in the physical output axes.
PANEL_INLINE panel_point_t panel_forward(unsigned pose,int x,int y,int w,int h) {
    panel_point_t p;
    switch(pose&3) {
    case 1:p.x=h-1-y;p.y=x;break;
    case 2:p.x=w-1-x;p.y=h-1-y;break;
    case 3:p.x=y;p.y=w-1-x;break;
    default:p.x=x;p.y=y;break;
    }
    if(pose&4)p.x=((pose&1)?h:w)-1-p.x;
    if(pose&8)p.y=((pose&1)?w:h)-1-p.y;
    return p;
}
PANEL_INLINE panel_point_t panel_inverse(unsigned pose,int x,int y,int w,int h) {
    if(pose&4)x=((pose&1)?h:w)-1-x;
    if(pose&8)y=((pose&1)?w:h)-1-y;
    panel_point_t p;
    switch(pose&3) {
    case 1:p.x=y;p.y=h-1-x;break;
    case 2:p.x=w-1-x;p.y=h-1-y;break;
    case 3:p.x=w-1-y;p.y=x;break;
    default:p.x=x;p.y=y;break;
    }
    return p;
}
// Bounded SRAM destination; tiled traversal keeps rotated PSRAM reads local.
PANEL_INLINE void panel_copy_rgb565(uint16_t *to,const uint16_t *from,unsigned start,
                                   unsigned count,unsigned w,unsigned h,unsigned pose) {
    const unsigned first=start/w,last=(start+count+w-1)/w;
    const unsigned source_w=pose&1?h:w,source_h=pose&1?w:h;
    for(unsigned tile=0;tile<w;tile+=8)for(unsigned y=first;y<last;++y)
        for(unsigned x=tile;x<w&&x<tile+8;++x) {
            const unsigned at=y*w+x;
            if(at<start||at>=start+count)continue;
            const panel_point_t p=panel_inverse(pose,x,y,(int)source_w,(int)source_h);
            to[at-start]=from[(unsigned)p.y*source_w+(unsigned)p.x];
        }
}
#undef PANEL_INLINE
