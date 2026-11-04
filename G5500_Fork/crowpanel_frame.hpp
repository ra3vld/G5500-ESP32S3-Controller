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
  static constexpr unsigned MaxPatches=32;
  struct Patch {uint16_t left,top,right,bottom;};
  uint16_t *front_=nullptr,*back_=nullptr;
  unsigned width_=0,height_=0,pose_=0;
  std::array<Patch,MaxPatches> dirty_{};
  unsigned dirtyCount_=0;
  void clearDirty() {dirtyCount_=0;}
  void addDirty(Patch patch) {
    for(unsigned i=0;i<dirtyCount_;++i) {
      auto &known=dirty_[i];
      if(known.left<=patch.left && known.top<=patch.top &&
          known.right>=patch.right && known.bottom>=patch.bottom)return;
    }
    if(dirtyCount_<dirty_.size())dirty_[dirtyCount_++]=patch;
    else {
      // A bounded fallback still copies every changed pixel if a caller sends
      // more patches than LVGL's normal invalid-area list can hold.
      auto &last=dirty_.back();
      last.left=std::min(last.left,patch.left);last.right=std::max(last.right,patch.right);
      last.top=std::min(last.top,patch.top);last.bottom=std::max(last.bottom,patch.bottom);
    }
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
      // A full-height transpose rereads distant PSRAM cache lines per column.
      // Tiles keep both source rows and neighbouring physical writes nearby.
      constexpr unsigned Tile=16;
      for(unsigned top=0;top<height;top+=Tile)
        for(unsigned left=0;left<width;left+=Tile)
          for(unsigned col=left;col<std::min(left+Tile,width);++col)
            for(unsigned row=top;row<std::min(top+Tile,height);++row)
              back_[first+int(col)*stepX+int(row)*stepY]=pixels[row*stride+col];
    }
    const auto end=panel_forward(pose_,x+int(width)-1,y+int(height)-1,int(logicalWidth),int(logicalHeight));
    addDirty({uint16_t(std::min(start.x,end.x)),uint16_t(std::min(start.y,end.y)),
        uint16_t(std::max(start.x,end.x)+1),uint16_t(std::max(start.y,end.y)+1)});
    return true;
  }
  // Call only after DMA releases the previous frame. Both buffers then contain
  // the same image before the next set of patches, including untouched pixels.
  size_t presented() {
    std::swap(front_,back_);
    size_t copied=0;
    for(unsigned i=0;i<dirtyCount_;++i) {
      const auto patch=dirty_[i];
      const unsigned count=patch.right-patch.left;
      for(unsigned y=patch.top;y<patch.bottom;++y) {
        const unsigned at=y*width_+patch.left;
        std::memcpy(back_+at,front_+at,count*sizeof(uint16_t));copied+=count;
      }
    }
    clearDirty();return copied;
  }
};
}
