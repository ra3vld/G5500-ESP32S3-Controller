// Compose changed regions into a double-buffered RGB panel. GPL-3.0.
#pragma once
#include "panel_pose.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace forkrot::crowpanel {
class FrameComposer {
  static constexpr unsigned MaxRows=480;
  uint16_t *front_=nullptr,*back_=nullptr;
  unsigned width_=0,height_=0,pose_=0;
  std::array<uint16_t,MaxRows> left_{},right_{};
  void clearDirty() {
    for(unsigned y=0;y<height_;++y){left_[y]=uint16_t(width_);right_[y]=0;}
  }
public:
  bool begin(uint16_t *front,uint16_t *back,unsigned width,unsigned height,unsigned pose) {
    if(!front||!back||front==back||!width||!height||height>MaxRows||width>65535||pose>15)return false;
    front_=front;back_=back;width_=width;height_=height;pose_=pose;clearDirty();return true;
  }
  uint16_t *pending() const {return back_;}
  bool blit(int x,int y,unsigned width,unsigned height,const uint16_t *pixels,unsigned stride=0) {
    const unsigned logicalWidth=pose_&1?height_:width_,logicalHeight=pose_&1?width_:height_;
    if(!stride)stride=width;
    if(!back_||!pixels||x<0||y<0||!width||!height||stride<width||unsigned(x)>=logicalWidth||unsigned(y)>=logicalHeight||
       width>logicalWidth-unsigned(x)||height>logicalHeight-unsigned(y))return false;
    const auto start=panel_forward(pose_,x,y,int(logicalWidth),int(logicalHeight));
    const auto along=panel_forward(pose_,x+1,y,int(logicalWidth),int(logicalHeight));
    const auto down=panel_forward(pose_,x,y+1,int(logicalWidth),int(logicalHeight));
    const int stepX=(along.y-start.y)*int(width_)+along.x-start.x;
    const int stepY=(down.y-start.y)*int(width_)+down.x-start.x;
    const int first=start.y*int(width_)+start.x;
    if(!(pose_&1)) {
      for(unsigned row=0;row<height;++row) {
        const int at=first+int(row)*stepY;
        if(stepX==1)std::memcpy(back_+at,pixels+row*stride,width*sizeof(uint16_t));
        else for(unsigned col=0;col<width;++col)back_[at+int(col)*stepX]=pixels[row*stride+col];
      }
    } else {
      // Write neighbouring physical pixels together, without per-pixel geometry.
      for(unsigned col=0;col<width;++col)
        for(unsigned row=0;row<height;++row)
          back_[first+int(col)*stepX+int(row)*stepY]=pixels[row*stride+col];
    }
    const auto end=panel_forward(pose_,x+int(width)-1,y+int(height)-1,int(logicalWidth),int(logicalHeight));
    const unsigned l=unsigned(std::min(start.x,end.x)),r=unsigned(std::max(start.x,end.x))+1;
    for(int row=std::min(start.y,end.y);row<=std::max(start.y,end.y);++row) {
      left_[row]=uint16_t(std::min(unsigned(left_[row]),l));
      right_[row]=uint16_t(std::max(unsigned(right_[row]),r));
    }
    return true;
  }
  // Call only after DMA releases the previous frame. Both buffers then contain
  // the same image before the next set of patches, including untouched pixels.
  size_t presented() {
    std::swap(front_,back_);
    size_t copied=0;
    for(unsigned y=0;y<height_;++y)if(left_[y]<right_[y]) {
      const unsigned count=right_[y]-left_[y],at=y*width_+left_[y];
      std::memcpy(back_+at,front_+at,count*sizeof(uint16_t));copied+=count;
    }
    clearDirty();return copied;
  }
};
}
