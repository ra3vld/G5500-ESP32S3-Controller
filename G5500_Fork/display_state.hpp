// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include "board.hpp"
#include "display_settings.hpp"
#include <cmath>
#include <cstdlib>
namespace forkrot {
struct TouchCalibration {
  bool touchCalValid = false, touchSwapXY = true;
  int touchX0Raw = board::TOUCH_RAW_Y_MIN, touchX1Raw = board::TOUCH_RAW_Y_MAX;
  int touchY0Raw = board::TOUCH_RAW_X_MAX, touchY1Raw = board::TOUCH_RAW_X_MIN;
};
inline bool fitTouchCalibration(TouchCalibration &cal,const int (&rawX)[4],const int (&rawY)[4],const DisplaySettings &settings) {
  constexpr int xs[]={20,299,299,20},ys[]={20,20,219,219};
  panel_point_t target[4];
  for(unsigned i=0;i<4;++i) {
    if(rawX[i]<0||rawX[i]>4095||rawY[i]<0||rawY[i]>4095)return false;
    target[i]=settings.uncorrectedTouch(xs[i]*int(settings.width(320,240)-1)/319,
        ys[i]*int(settings.height(320,240)-1)/239,320,240);
  }
  auto covariance=[&](const int (&raw)[4],bool y) {
    int64_t a=0,b=0,ab=0;for(unsigned i=0;i<4;++i){const int t=y?target[i].y:target[i].x;a+=t;b+=raw[i];ab+=int64_t(t)*raw[i];}
    return 4*ab-a*b;
  };
  TouchCalibration next=cal;next.touchSwapXY=std::llabs(covariance(rawY,false))>std::llabs(covariance(rawX,false));
  auto fit=[&](const int (&raw)[4],bool y,int first,int last,int &lo,int &hi) {
    int64_t t=0,r=0,tt=0;for(unsigned i=0;i<4;++i){const int v=y?target[i].y:target[i].x;t+=v;r+=raw[i];tt+=v*v;}
    const int64_t denominator=4*tt-t*t;if(!denominator)return false;
    const double slope=double(covariance(raw,y))/double(denominator);
    lo=int(std::lround(double(r)/4+slope*(first-double(t)/4)));
    hi=int(std::lround(double(r)/4+slope*(last-double(t)/4)));
    return lo>=0&&lo<=4095&&hi>=0&&hi<=4095&&std::abs(hi-lo)>500;
  };
  if(!fit(next.touchSwapXY?rawY:rawX,false,20,299,next.touchX0Raw,next.touchX1Raw) ||
      !fit(next.touchSwapXY?rawX:rawY,true,20,219,next.touchY0Raw,next.touchY1Raw))return false;
  next.touchCalValid=true;cal=next;return true;
}
} // namespace forkrot
