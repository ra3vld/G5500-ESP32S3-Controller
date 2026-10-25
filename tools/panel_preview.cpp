// Exercise the real LVGL UI at native resolutions, including gesture geometry.
#include "panel_ui.hpp"
#include <vector>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>
using namespace forkrot;
static int width,height;static bool roundPanel;
static std::vector<uint16_t> image;
static unsigned flushes;
static void flush(lv_display_t *display,const lv_area_t *a,uint8_t *data) {
  ++flushes;auto *pixel=reinterpret_cast<uint16_t *>(data);
  assert(a->x1>=0 && a->y1>=0 && a->x2<width && a->y2<height);
  for(int y=a->y1;y<=a->y2;++y) for(int x=a->x1;x<=a->x2;++x) image[y*width+x]=*pixel++;
  lv_display_flush_ready(display);
}
static void save(const char *path) {
  FILE *f=std::fopen(path,"wb");assert(f);std::fprintf(f,"P6\n%d %d\n255\n",width,height);
  for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
    auto c=image[y*width+x];
    if(roundPanel && (x-width/2)*(x-width/2)+(y-height/2)*(y-height/2)>(width/2)*(width/2)) c=0;
    uint8_t rgb[]={uint8_t(((c>>11)&31)*255/31),uint8_t(((c>>5)&63)*255/63),uint8_t((c&31)*255/31)};
    assert(std::fwrite(rgb,1,3,f)==3);
  }
  std::fclose(f);
}
static void settle(lv_display_t *d) {lv_tick_inc(250);lv_timer_handler();lv_refr_now(d);}
static bool inside(const panel::TouchRect &r) {return r.x+r.width<=width && r.y+r.height<=height;}
static panel::TouchEvent press(panel::TouchGesture &gesture,const panel::TouchLayout &layout,const panel::TouchRect &r,uint32_t now) {
  assert(r.width && r.height && inside(r));return gesture.sample(true,r.x+r.width/2,r.y+r.height/2,layout,now);
}
int main(int argc,char **argv) {
  assert(argc==5);width=std::atoi(argv[2]);height=std::atoi(argv[3]);roundPanel=std::atoi(argv[4]);
  lv_init();auto *d=lv_display_create(width,height);lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);
  std::vector<uint16_t> buffer(width*16+8,0x5aa5);image.resize(width*height);
  lv_display_set_buffers(d,buffer.data()+4,nullptr,width*16*2,LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(d,flush);
  PanelUi ui;ui.create(roundPanel);PanelView v;
  v.hud=true;v.dualAxis=true;v.autoMode=true;v.feedback=true;v.secondaryFeedback=true;
  v.current=121;v.secondaryCurrent=35;v.targetValid=true;v.secondaryTargetValid=true;
  v.target=210;v.secondaryTarget=80;v.wifiConnected=true;v.wifiRssi=-55;
  std::strcpy(v.hudName,"Дача · G-5500");std::strcpy(v.hudAddress,"hud:192.168.3.234");
  std::strcpy(v.status,"Остановлен");std::strcpy(v.ip,"STA: 192.168.3.234");std::strcpy(v.heap,"Heap: 123 КБ");
  char path[512];
  const auto snapshot=[&](const char *name) {
    ui.update(v);settle(d);std::snprintf(path,sizeof path,"%s/%dx%d-%s.ppm",argv[1],width,height,name);save(path);
    const auto pixels=image;lv_obj_invalidate(lv_screen_active());lv_refr_now(d);assert(pixels==image);
    for(unsigned i=0;i<4;++i) assert(buffer[i]==0x5aa5 && buffer[buffer.size()-1-i]==0x5aa5);
  };
  snapshot("dual");
  v.elevation=true;std::swap(v.current,v.secondaryCurrent);std::swap(v.target,v.secondaryTarget);
  v.minimum=-30;v.maximum=210;snapshot("elevation");
  v.elevation=false;std::swap(v.current,v.secondaryCurrent);std::swap(v.target,v.secondaryTarget);
  v.minimum=0;v.maximum=450;ui.update(v);settle(d);
  auto layout=ui.touchLayout();assert(inside(layout.mode) && inside(layout.current) && inside(layout.target));
  for(unsigned i=0;i<4;++i) {
    if(!layout.arrows[i].width) continue;
    panel::TouchGesture input;input.sample(false,0,0,layout,0);
    const auto held=press(input,layout,layout.arrows[i],10);assert(held.manual && !held.release);
    const auto released=input.sample(false,0,0,layout,20);assert(released.manual && released.release);
  }
  const auto idle=flushes;for(int i=0;i<5;++i) {ui.update(v);settle(d);}assert(flushes==idle);
  v.autoMode=false;v.pending=true;snapshot("confirm");
  v.pending=false;v.movingNow=true;v.azDirection=1;std::strcpy(v.status,"Вращение");snapshot("moving");
  layout=ui.touchLayout();panel::TouchGesture input;
  input.sample(false,0,0,layout,0);assert(press(input,layout,layout.stop,10).action==panel::TouchAction::Stop);
  input.sample(false,0,0,layout,20);assert(press(input,layout,layout.stop,30).action==panel::TouchAction::Stop);
  // STOP remains reachable when ordinary commands are disabled, including the speed page.
  v.enabled=false;v.speedScreen=true;v.speedAvailable[0]=v.speedAvailable[1]=true;
  snapshot("speed-stop");layout=ui.touchLayout();panel::TouchGesture blocked;
  assert(press(blocked,layout,layout.stop,40).action==panel::TouchAction::Stop);
  v.enabled=true;v.movingNow=false;v.azDirection=0;std::strcpy(v.status,"Остановлен");snapshot("speed");
  layout=ui.touchLayout();panel::TouchGesture speed;
  speed.sample(false,0,0,layout,50);
  const auto e=press(speed,layout,layout.speedTracks[1],60);assert(e.action==panel::TouchAction::SpeedValue && e.axis==1);
  v.speedScreen=false;v.hudSelector=true;
  std::strcpy(v.hudRows[0],"Мачта · G-800DXA\n120° · Стоп");std::strcpy(v.hudRows[1],"Дача · G-5500\n121° / 35° · Стоп");std::strcpy(v.hudRows[2],"Антенна · G-800SA\nНет связи");snapshot("menu");
  v.movingNow=true;snapshot("menu-stop");assert(ui.touchLayout().stop.width);v.movingNow=false;
  v.hudSelector=false;v.feedback=v.secondaryFeedback=v.targetValid=v.secondaryTargetValid=false;
  std::strcpy(v.status,"Нет связи с повороткой");snapshot("offline");
  v.hud=false;v.settingsAvailable=DISPLAY_KIND==DISPLAY_ILI9341 && !roundPanel;v.dualAxis=false;v.feedback=true;v.current=359.9;
  v.targetValid=true;v.target=450;v.minimum=0;v.maximum=450;std::strcpy(v.status,"Остановлен");snapshot("single");
  v.current=-359;v.target=721;v.minimum=-360;v.maximum=1080;snapshot("wide");
  std::strcpy(v.ip,"STA: 255.255.255.255  AP: 192.168.4.1");snapshot("network");
  v.statusFault=true;std::strcpy(v.status,"Ошибка AZ: нет изменения положения");snapshot("fault");
  std::printf("PASS %dx%d: native LVGL rendering, buffer bounds, idle redraw, arrows/release, repeated STOP and speed controls\n",width,height);
}
