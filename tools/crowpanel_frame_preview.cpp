// Compare rotated dirty-region scanout with the unrotated LVGL image.
#include "panel_ui.hpp"
#include "crowpanel_frame.hpp"
#include <vector>
#include <cassert>
#include <cstdio>
#include <cstring>

struct Pipeline {
  std::vector<uint16_t> a=std::vector<uint16_t>(480*480),b=a,image=a,logicalA=a,logicalB=a;
  forkrot::crowpanel::FrameComposer composer;
  bool patches;
  unsigned pose,frames=0;
  lv_display_t *display;
  forkrot::PanelUi ui;
  static void flush(lv_display_t *d,const lv_area_t *area,uint8_t *data) {
    auto &self=*static_cast<Pipeline *>(lv_display_get_user_data(d));
    if(self.patches) {
      assert(self.composer.blit(area->x1,area->y1,unsigned(area->x2-area->x1+1),
          unsigned(area->y2-area->y1+1),reinterpret_cast<uint16_t *>(data)+area->y1*480+area->x1,480));
      if(lv_display_flush_is_last(d)) {
        std::copy(self.composer.pending(),self.composer.pending()+480*480,self.image.begin());
        self.composer.presented();++self.frames;
      }
    } else if(lv_display_flush_is_last(d)) {
      std::memcpy(self.image.data(),data,self.image.size()*2);++self.frames;
    }
    lv_display_flush_ready(d);
  }
  Pipeline(bool partial,unsigned orientation):patches(partial),pose(orientation) {
    display=lv_display_create(480,480);lv_display_set_default(display);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_user_data(display,this);lv_display_set_flush_cb(display,flush);
    if(patches) {
      assert(composer.begin(a.data(),b.data(),480,480,pose));
      lv_display_set_buffers(display,logicalA.data(),logicalB.data(),logicalA.size()*2,LV_DISPLAY_RENDER_MODE_DIRECT);
    } else lv_display_set_buffers(display,a.data(),b.data(),a.size()*2,LV_DISPLAY_RENDER_MODE_DIRECT);
    ui.create();
  }
  ~Pipeline(){lv_display_delete(display);}
  void update(const forkrot::PanelView &view){lv_display_set_default(display);ui.update(view);}
  void sameImage(const Pipeline &reference) const {
    for(int y=0;y<480;++y)for(int x=0;x<480;++x) {
      const auto p=panel_forward(pose,x,y,480,480);
      if(image[p.y*480+p.x]!=reference.image[y*480+x]) {
        std::fprintf(stderr,"pose=%u different pixel %d,%d\n",pose,x,y);assert(false);
      }
    }
  }
  void save(const char *directory)const {
    char path[512];std::snprintf(path,sizeof path,"%s/frame-pose-%u.ppm",directory,pose);
    auto *file=std::fopen(path,"wb");assert(file);std::fprintf(file,"P6\n480 480\n255\n");
    for(const auto value:image){uint8_t rgb[]={uint8_t(((value>>11)&31)*255/31),uint8_t(((value>>5)&63)*255/63),uint8_t((value&31)*255/31)};std::fwrite(rgb,1,3,file);}
    std::fclose(file);
  }
};
int main(int argc,char **argv) {
  assert(argc==2);lv_init();
  for(unsigned pose:{0u,1u,2u,3u,15u}) {
    Pipeline reference(false,0),patches(true,pose);
    forkrot::PanelView view;view.hud=true;view.dualAxis=true;view.feedback=view.secondaryFeedback=true;
    view.targetValid=view.secondaryTargetValid=true;view.current=51;view.target=90;view.secondaryCurrent=25;view.secondaryTarget=40;
    view.wifiConnected=true;view.wifiRssi=-60;
    std::strcpy(view.hudName,"Имитация");std::strcpy(view.hudAddress,"HUD");
    std::strcpy(view.ip,"192.168.3.236");std::strcpy(view.heap,"Heap: 60 КБ");
    const auto check=[&] {
      reference.update(view);patches.update(view);
      for(int i=0;i<4;++i){lv_tick_inc(60);lv_timer_handler();}
      lv_refr_now(reference.display);lv_refr_now(patches.display);patches.sameImage(reference);
      const auto image=patches.image;lv_obj_invalidate(patches.ui.root());lv_refr_now(patches.display);
      assert(patches.image==image); // No stale pixels after buffer handoff.
    };
    check();
    for(unsigned step=0;step<24;++step) {
      view.current+=.5f;view.target+=.2f;view.secondaryCurrent+=.25f;view.movingNow=step<18;
      view.azDirection=step<18?1:0;view.elDirection=step<18?1:0;
      std::snprintf(view.heap,sizeof view.heap,"Heap: %u КБ",60+step%2);check();
    }
    view.pending=true;view.autoMode=false;check();
    view.elevation=true;check();
    view.speedScreen=true;view.speedAvailable[0]=view.speedAvailable[1]=true;view.speedPercent[0]=70;view.speedPercent[1]=30;check();
    view.speedScreen=false;view.menu.open=true;view.menu.local=true;view.menu.brightnessAvailable=true;view.menu.brightness=70;
    std::strcpy(view.menu.device,"Этот HUD");std::strcpy(view.menu.info,"Память: 60 КБ");check();
    view.menu.open=false;view.pending=false;view.elevation=false;check();patches.save(argv[1]);
    std::printf("PASS pose=%u: direct/rotated pixels match, motion/STOP, telemetry, confirmation, axis, speed/menu and full redraw\n",pose);
  }
}
