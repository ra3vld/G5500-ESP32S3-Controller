#include "display_settings.hpp"
#include "display_state.hpp"
#include <cassert>
#include <vector>
#include <cstdio>
using namespace forkrot;
int main() {
  for(unsigned pose=0;pose<16;++pose)for(auto shape:{panel_point_t{3,2},{8,5},{32,32}}) {
    const int w=shape.x,h=shape.y,ow=pose&1?h:w,oh=pose&1?w:h;
    std::vector<uint16_t> source(w*h),expected(w*h),seen(w*h);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
      const auto p=panel_forward(pose,x,y,w,h);assert(p.x>=0&&p.x<ow&&p.y>=0&&p.y<oh);
      const auto q=panel_inverse(pose,p.x,p.y,w,h);assert(q.x==x&&q.y==y);
      assert(!seen[p.y*ow+p.x]++);source[y*w+x]=uint16_t(y*w+x);expected[p.y*ow+p.x]=uint16_t(y*w+x);
    }
    for(unsigned start=0;start<source.size();start+=3) {
      const auto count=std::min<size_t>(17,source.size()-start);std::vector<uint16_t> out(count+2,0xa55a);
      panel_copy_rgb565(out.data()+1,source.data(),start,count,ow,oh,pose);
      assert(out.front()==0xa55a&&out.back()==0xa55a);
      for(unsigned i=0;i<count;++i)assert(out[i+1]==expected[start+i]);
    }
  }
  for(unsigned screen=0;screen<16;++screen)for(unsigned touch=0;touch<16;++touch) {
    DisplaySettings s;s.screen=screen;s.touch=touch;
    for(auto p:{panel_point_t{0,0},{479,319},{200,101},{479,0},{0,319}}) {
      const auto q=s.logical(p.x,p.y,480,320),back=s.physical(q.x,q.y,480,320);
      assert(back.x==p.x&&back.y==p.y);
      const auto corrected=s.correctedTouch(p.x,p.y,480,320);
      assert(corrected.x>=0&&unsigned(corrected.x)<s.width(480,320));
      assert(corrected.y>=0&&unsigned(corrected.y)<s.height(480,320));
      if(!touch)assert(corrected.x==q.x&&corrected.y==q.y);
    }
    s.revision=0x12345678;auto bytes=encodeDisplay(s);DisplaySettings restored;
    assert(decodeDisplay(bytes,restored)&&restored.samePose(s)&&restored.revision==s.revision);
    for(unsigned i=0;i<bytes.size();++i){auto bad=bytes;bad[i]^=1;DisplaySettings unchanged=s;assert(!decodeDisplay(bad,unchanged));assert(unchanged.samePose(s));}
  }
  const uint8_t ili[]={0x28,0x88,0xe8,0x48};
  for(unsigned r=0;r<4;++r){DisplaySettings s;s.screen=r;assert(s.madctl(0x28)==ili[r]);}
  assert(DisplaySettings{}.madctl(0xa0)==0xa0);
  for(unsigned screen=0;screen<16;++screen)for(unsigned correction=0;correction<16;++correction)for(bool swap:{false,true}) {
    DisplaySettings s;s.screen=screen;s.touch=correction;int rawX[4],rawY[4];
    constexpr int xs[]={20,299,299,20},ys[]={20,20,219,219};
    for(unsigned i=0;i<4;++i) {
      const auto p=s.uncorrectedTouch(xs[i]*int(s.width(320,240)-1)/319,ys[i]*int(s.height(320,240)-1)/239,320,240);
      rawX[i]=100+(swap?p.y*12:p.x*10);rawY[i]=200+(swap?p.x*10:p.y*12);
    }
    TouchCalibration cal;assert(fitTouchCalibration(cal,rawX,rawY,s)&&cal.touchSwapXY==swap);
    assert(std::abs(cal.touchX0Raw-(swap?400:300))<=1);
    assert(std::abs(cal.touchX1Raw-(swap?3190:3090))<=1);
    assert(std::abs(cal.touchY0Raw-(swap?340:440))<=1);
    assert(std::abs(cal.touchY1Raw-(swap?2728:2828))<=1);
  }
  std::puts("PASS: all display/touch poses, rectangular corner round-trips, tiled RGB565 refill, MADCTL composition and checked records");
}
