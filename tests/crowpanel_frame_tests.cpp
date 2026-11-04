#include "crowpanel_frame.hpp"
#include <cassert>
#include <vector>
#include <cstdio>
using forkrot::crowpanel::FrameComposer;
int main() {
  {
    std::vector<uint16_t> a(64*48),b(a),patch(4,0x1234);
    FrameComposer frame;assert(frame.begin(a.data(),b.data(),64,48,0));
    assert(frame.blit(1,2,2,2,patch.data()));assert(frame.blit(60,2,2,2,patch.data()));
    assert(frame.presented()==8); // Do not copy the unchanged gap between two patches.
    assert(a==b);
    // More than 32 patches remain bounded and preserve the whole back buffer.
    for(unsigned i=0;i<100;++i)assert(frame.blit(i%64,i/64,1,1,patch.data()));
    frame.presented();assert(a==b);
  }
  for(unsigned pose=0;pose<16;++pose) {
    constexpr unsigned w=96,h=80,stride=103;
    const unsigned lw=pose&1?h:w,lh=pose&1?w:h;
    std::vector<uint16_t> a(w*h+2,0x5aa5),b(a),pixels(stride*lh);
    for(unsigned i=0;i<pixels.size();++i)pixels[i]=uint16_t(i*13+7);
    FrameComposer frame;assert(frame.begin(a.data()+1,b.data()+1,w,h,pose));
    assert(frame.blit(1,2,lw-3,lh-5,pixels.data(),stride));
    const auto pending=frame.pending();
    for(unsigned y=0;y<lh-5;++y)for(unsigned x=0;x<lw-3;++x) {
      const auto p=panel_forward(pose,int(x+1),int(y+2),int(lw),int(lh));
      assert(pending[p.y*w+p.x]==pixels[y*stride+x]);
    }
    assert(frame.presented()==(lw-3)*(lh-5));assert(a==b);
    assert(a.front()==0x5aa5&&a.back()==0x5aa5&&b.front()==0x5aa5&&b.back()==0x5aa5);
  }
  for(unsigned pose=0;pose<16;++pose)for(auto shape:{panel_point_t{13,9},panel_point_t{32,32}}) {
    const unsigned w=unsigned(shape.x),h=unsigned(shape.y),lw=pose&1?h:w,lh=pose&1?w:h;
    std::vector<uint16_t> a(w*h+2,0x5aa5),b(a),expected(w*h,0),patch(20);
    std::fill(a.begin()+1,a.end()-1,0);std::fill(b.begin()+1,b.end()-1,0);
    FrameComposer frame;assert(frame.begin(a.data()+1,b.data()+1,w,h,pose));
    for(unsigned step=0;step<60;++step) {
      const auto visible=frame.pending()==a.data()+1?b:a;
      const unsigned x=step%(lw-4),y=(step*3)%(lh-3),pw=step%4+1,ph=step%3+1;
      for(unsigned i=0;i<pw*ph;++i)patch[i]=uint16_t(step*16+i+1);
      const auto oldPending=frame.pending();
      assert(frame.blit(int(x),int(y),pw,ph,patch.data()));
      for(unsigned row=0;row<ph;++row)for(unsigned col=0;col<pw;++col) {
        const auto p=panel_forward(pose,int(x+col),int(y+row),int(lw),int(lh));
        expected[p.y*w+p.x]=patch[row*pw+col];
      }
      assert((oldPending==a.data()+1?b:a)==visible); // Active DMA source stays untouched.
      assert(!frame.blit(-1,0,1,1,patch.data()));
      assert(!frame.blit(int(lw),0,1,1,patch.data()));
      assert(frame.presented()==pw*ph); // No full-frame synchronization for a small patch.
      assert(frame.pending()!=oldPending);
      for(unsigned i=0;i<w*h;++i)assert(a[i+1]==expected[i]&&b[i+1]==expected[i]);
      assert(a.front()==0x5aa5&&a.back()==0x5aa5&&b.front()==0x5aa5&&b.back()==0x5aa5);
    }
    // Several separate strips in one frame preserve all prior and intervening pixels.
    assert(frame.blit(1,1,2,2,patch.data()));
    assert(frame.blit(2,2,3,2,patch.data()));
    frame.presented();assert(a==b);
    std::vector<uint16_t> padded(20,0x7777);
    padded[0]=11;padded[1]=12;padded[7]=21;padded[8]=22;
    assert(!frame.blit(1,1,2,2,padded.data(),1));
    assert(frame.blit(1,1,2,2,padded.data(),7));
    const auto pending=frame.pending();
    for(unsigned row=0;row<2;++row)for(unsigned col=0;col<2;++col) {
      const auto p=panel_forward(pose,int(1+col),int(1+row),int(lw),int(lh));
      assert(pending[p.y*w+p.x]==padded[row*7+col]);
    }
    assert(frame.presented()==4);assert(a==b);
  }
  std::puts("PASS: 16 rotated/mirrored patch poses, protected DMA source, partial frame synchronization and bounds");
}
