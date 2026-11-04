// Benchmark production LVGL updates and count the work submitted to scanout.
#include "panel_ui.hpp"
#include "crowpanel_frame.hpp"
#include <chrono>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace forkrot;
struct Bench {
  std::vector<uint16_t> a=std::vector<uint16_t>(480*480),b=a,front=a,back=a;
  crowpanel::FrameComposer composer;
  uint64_t pixels=0,patches=0,copied=0;
  lv_display_t *display;
  PanelUi ui;
  static void flush(lv_display_t *d,const lv_area_t *area,uint8_t *data) {
    auto &s=*static_cast<Bench *>(lv_display_get_user_data(d));
    const unsigned w=area->x2-area->x1+1,h=area->y2-area->y1+1;
    s.pixels+=w*h;++s.patches;
    assert(s.composer.blit(area->x1,area->y1,w,h,reinterpret_cast<uint16_t *>(data)+area->y1*480+area->x1,480));
    if(lv_display_flush_is_last(d))s.copied+=s.composer.presented();
    lv_display_flush_ready(d);
  }
  Bench() {
    display=lv_display_create(480,480);lv_display_set_default(display);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_user_data(display,this);lv_display_set_flush_cb(display,flush);
    lv_display_set_buffers(display,a.data(),b.data(),a.size()*2,LV_DISPLAY_RENDER_MODE_DIRECT);
    assert(composer.begin(front.data(),back.data(),480,480,1));ui.create();
  }
  ~Bench(){lv_display_delete(display);}
  void step(const PanelView &v){ui.update(v);lv_tick_inc(20);lv_timer_handler();lv_refr_now(display);}
};
int main(int argc,char **) {
  lv_init();
  for(const char *name:{"idle","rssi","heap","fractional","motion","menu","speed"}) {
    Bench b;PanelView v;v.hud=true;v.dualAxis=true;v.feedback=v.secondaryFeedback=true;
    v.targetValid=v.secondaryTargetValid=true;v.current=144;v.secondaryCurrent=19;v.target=180;v.secondaryTarget=30;
    v.sourceId=101;v.wifiConnected=true;v.wifiRssi=-60;v.status[0]=0;
    std::strcpy(v.hudName,"esp32_test");std::strcpy(v.hudAddress,"192.168.3.128");
    std::strcpy(v.heap,"Heap: 70 КБ");std::strcpy(v.ip,"192.168.3.127");
    if(!std::strcmp(name,"motion")){v.movingNow=true;v.azDirection=1;}
    if(!std::strcmp(name,"menu")){v.menu.open=true;v.menu.local=true;v.menu.brightnessAvailable=true;std::strcpy(v.menu.device,"Этот HUD");}
    if(!std::strcmp(name,"speed")){v.speedScreen=true;v.speedAvailable[0]=v.speedAvailable[1]=true;v.speedPercent[0]=v.speedPercent[1]=50;}
    for(unsigned i=0;i<20;++i)b.step(v);
    b.pixels=b.patches=b.copied=0;uint64_t updateUs=0,renderUs=0,maxUs=0;
    for(unsigned i=0;i<100;++i) {
      if(!std::strcmp(name,"rssi"))v.wifiRssi=-60-int(i%2);
      if(!std::strcmp(name,"heap"))std::snprintf(v.heap,sizeof v.heap,"Heap: %u КБ",70+i%2);
      if(!std::strcmp(name,"fractional"))v.current=144+.001f*i;
      if(!std::strcmp(name,"motion")){v.current+=.02f;std::snprintf(v.heap,sizeof v.heap,"Heap: %u КБ",70+i%2);}
      if(!std::strcmp(name,"menu"))std::snprintf(v.menu.info,sizeof v.menu.info,"Аптайм: %u",i/10);
      if(!std::strcmp(name,"speed"))v.wifiRssi=-60-int(i%2);
      const auto a=std::chrono::steady_clock::now();b.ui.update(v);
      const auto c=std::chrono::steady_clock::now();lv_tick_inc(20);lv_timer_handler();lv_refr_now(b.display);
      const auto d=std::chrono::steady_clock::now();
      const auto update=std::chrono::duration_cast<std::chrono::microseconds>(c-a).count();
      const auto render=std::chrono::duration_cast<std::chrono::microseconds>(d-c).count();
      updateUs+=update;renderUs+=render;maxUs=std::max(maxUs,uint64_t(update+render));
    }
    uint64_t hash=14695981039346656037ull;for(uint16_t x:b.a){hash^=x;hash*=1099511628211ull;}
    std::printf("%s update_us=%llu render_us=%llu max_us=%llu patches=%llu pixels=%llu copied=%llu hash=%016llx\n",name,
      (unsigned long long)updateUs,(unsigned long long)renderUs,(unsigned long long)maxUs,(unsigned long long)b.patches,
      (unsigned long long)b.pixels,(unsigned long long)b.copied,(unsigned long long)hash);
    if(argc>1) {
      if(!std::strcmp(name,"idle")||!std::strcmp(name,"rssi")||!std::strcmp(name,"speed"))assert(b.pixels==0);
      if(!std::strcmp(name,"menu"))assert(b.pixels<480*480);
      if(!std::strcmp(name,"motion"))assert(b.copied==b.pixels);
    }
  }
  std::vector<uint16_t> source(480*480),a(source.size()),b(a);
  for(unsigned i=0;i<source.size();++i)source[i]=uint16_t(i*17+3);
  for(unsigned pose:{0u,1u,3u,5u}) {
    crowpanel::FrameComposer composer;assert(composer.begin(a.data(),b.data(),480,480,pose));
    const auto started=std::chrono::steady_clock::now();
    for(unsigned i=0;i<200;++i){assert(composer.blit(0,0,480,480,source.data()));composer.presented();}
    const auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count();
    std::printf("transpose pose=%u frames=200 us=%llu state_bytes=%zu\n",pose,(unsigned long long)us,sizeof(composer));
  }
}
