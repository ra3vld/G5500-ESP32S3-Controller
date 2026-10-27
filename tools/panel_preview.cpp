// Exercise the real LVGL UI at native resolutions, including gesture geometry.
#include "panel_ui.hpp"
#include "hud_background.hpp"
#include <vector>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <cmath>
using namespace forkrot;
static int width,height;static bool roundPanel;
static std::vector<uint16_t> image;
static unsigned flushes;
static size_t flushedPixels;
static void flush(lv_display_t *display,const lv_area_t *a,uint8_t *data) {
  ++flushes;auto *pixel=reinterpret_cast<uint16_t *>(data);
  flushedPixels+=size_t(a->x2-a->x1+1)*(a->y2-a->y1+1);
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
static bool overlaps(const panel::TouchRect &a,const panel::TouchRect &b) {
  return a.width&&b.width&&a.x<b.x+b.width&&b.x<a.x+a.width&&a.y<b.y+b.height&&b.y<a.y+a.height;
}
static void checkCurrentLayout(const PanelView &v,const panel::TouchLayout &layout) {
  if(v.speedScreen||v.hudSelector)return;
  const bool compact=std::min(width,height)<320;
  assert(layout.current.width && layout.current.height && inside(layout.current));
  assert(layout.outerRadius==std::min(width,height)/2+panel::DialTouchExtension);
  const panel::TouchRect controls[]={layout.current,layout.mode,layout.name,layout.axis,layout.settings,layout.stop,layout.target,
      layout.arrows[0],layout.arrows[1],layout.arrows[2],layout.arrows[3]};
  for(unsigned i=0;i<std::size(controls);++i) {
    assert(inside(controls[i]));
    for(unsigned j=0;j<i;++j) {
      if(j!=0 && i<7)continue; // Only the reading and arrows change their geometry.
      if(overlaps(controls[i],controls[j]))std::fprintf(stderr,"overlap %u/%u: %u,%u %ux%u / %u,%u %ux%u mask=%u\n",i,j,controls[i].x,controls[i].y,controls[i].width,controls[i].height,controls[j].x,controls[j].y,controls[j].width,controls[j].height,unsigned(v.visible));
      assert(!overlaps(controls[i],controls[j]));
    }
  }
  const auto &current=layout.current;
  const auto &left=layout.arrows[compact&&v.elevation?3:0],&right=layout.arrows[compact&&v.elevation?2:1];
  assert(left.width && right.width && left.x+left.width<=current.x && right.x>=current.x+current.width);
  assert(std::abs(int(left.y*2+left.height)-int(current.y*2+current.height))<=1);
  assert(std::abs(int(right.y*2+right.height)-int(current.y*2+current.height))<=1);
  if(!compact&&v.dualAxis) {
    const auto &up=layout.arrows[2],&down=layout.arrows[3];
    assert(up.width && down.width && up.y+up.height<=current.y && down.y>=current.y+current.height);
    assert(std::abs(int(up.x*2+up.width)-int(current.x*2+current.width))<=1);
    assert(std::abs(int(down.x*2+down.width)-int(current.x*2+current.width))<=1);
  }
}
static panel::TouchEvent press(panel::TouchGesture &gesture,const panel::TouchLayout &layout,const panel::TouchRect &r,uint32_t now) {
  assert(r.width && r.height && inside(r));return gesture.sample(true,r.x+r.width/2,r.y+r.height/2,layout,now);
}
int main(int argc,char **argv) {
  assert(argc==5);width=std::atoi(argv[2]);height=std::atoi(argv[3]);roundPanel=std::atoi(argv[4]);
  const bool compact=std::min(width,height)<320;
  lv_init();auto *d=lv_display_create(width,height);lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);
  std::vector<uint16_t> buffer(width*16+8,0x5aa5);image.resize(width*height);
  lv_display_set_buffers(d,buffer.data()+4,nullptr,width*16*2,LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(d,flush);
  PanelUi ui;ui.create(roundPanel);PanelView v;
  v.hud=true;v.dualAxis=true;v.autoMode=true;v.feedback=true;v.secondaryFeedback=true;
  v.current=121;v.secondaryCurrent=35;v.targetValid=true;v.secondaryTargetValid=true;
  v.target=210;v.secondaryTarget=80;v.wifiConnected=true;v.wifiRssi=-55;
  std::strcpy(v.hudName,"Дача · G-5500");std::strcpy(v.hudAddress,"hud:192.168.3.234");
  std::strcpy(v.status,"");std::strcpy(v.ip,"192.168.3.234");std::strcpy(v.heap,"Heap: 123 КБ");
  const auto findLabel=[&](auto &&self,lv_obj_t *object,const char *value)->lv_obj_t * {
    if(lv_obj_check_type(object,&lv_label_class) && !std::strcmp(lv_label_get_text(object),value))return object;
    for(uint32_t i=0;i<lv_obj_get_child_count(object);++i)if(auto *found=self(self,lv_obj_get_child(object,i),value))return found;
    return nullptr;
  };
  char path[512];
  const auto snapshot=[&](const char *name) {
    ui.update(v);settle(d);std::snprintf(path,sizeof path,"%s/%dx%d-%s.ppm",argv[1],width,height,name);save(path);
    if(std::getenv("PANEL_GEOMETRY")) {
      const auto &l=ui.touchLayout();
      std::printf("%s current=%u,%u %ux%u arrows",name,l.current.x,l.current.y,l.current.width,l.current.height);
      for(const auto &r:l.arrows)std::printf(" [%u,%u %ux%u]",r.x,r.y,r.width,r.height);
      std::puts("");std::fflush(stdout);
      std::snprintf(path,sizeof path,"%s/%dx%d-%s.geometry",argv[1],width,height,name);
      FILE *f=std::fopen(path,"w");assert(f);
      for(const auto &r:{l.current,l.arrows[0],l.arrows[1],l.arrows[2],l.arrows[3]})
        std::fprintf(f,"%u %u %u %u\n",r.x,r.y,r.width,r.height);
      std::fclose(f);
    }
    checkCurrentLayout(v,ui.touchLayout());
    const auto pixels=image;lv_obj_invalidate(lv_screen_active());lv_refr_now(d);assert(pixels==image);
    for(unsigned i=0;i<4;++i) assert(buffer[i]==0x5aa5 && buffer[buffer.size()-1-i]==0x5aa5);
    auto *stopLabel=findLabel(findLabel,ui.root(),"СТОП");assert(stopLabel);
    for(const char32_t *ch=U"СТОП";*ch;++ch) {
      lv_font_glyph_dsc_t glyph{};
      assert(lv_font_get_glyph_dsc(lv_obj_get_style_text_font(stopLabel,LV_PART_MAIN),&glyph,*ch,0));
    }
    assert(lv_obj_is_hidden(lv_obj_get_parent(stopLabel))==!v.movingNow);
    const auto &l=ui.touchLayout();
    assert(bool(l.stop.width)==v.movingNow && l.stopArea.width);
    if(v.movingNow) {
      assert(inside(l.stop));
      assert(l.stop.contains(lv_obj_get_x(lv_obj_get_parent(stopLabel))+lv_obj_get_x(stopLabel),
                             lv_obj_get_y(lv_obj_get_parent(stopLabel))+lv_obj_get_y(stopLabel)));
      assert(lv_obj_get_width(stopLabel)<=l.stop.width && lv_obj_get_height(stopLabel)<=l.stop.height);
      assert(lv_obj_is_hidden(findLabel(findLabel,ui.root(),v.ip)));
      assert(lv_obj_is_hidden(findLabel(findLabel,ui.root(),v.heap)));
      if(v.status[0])assert(lv_obj_is_hidden(findLabel(findLabel,ui.root(),v.status))==!v.statusFault);
      if(!v.speedScreen&&!v.hudSelector) {
        assert(!overlaps(l.stop,l.target));
        assert(l.stop.height>=std::min(width,height)/20);
      }
      panel::TouchGesture input;
      assert(press(input,l,l.stop,10).action==panel::TouchAction::Stop);
    }
  };
  snapshot("dual");
  {
    const auto saved=v;
    v.hudBackground=2;ui.update(v);settle(d);
    assert(lv_color_to_u32(lv_obj_get_style_bg_color(ui.root(),LV_PART_MAIN))==
           lv_color_to_u32(lv_color_hex(hudBackgroundColor(2))));
    for(unsigned color=1;color<hudBackgroundCount;++color) {
      v.sourceId=100+color;v.hudBackground=color;v.current=4;v.target=448;
      char name[40];std::snprintf(name,sizeof name,"hud-background-%u",color);snapshot(name);
      assert(lv_color_to_u32(lv_obj_get_style_bg_color(ui.root(),LV_PART_MAIN))==
             lv_color_to_u32(lv_color_hex(hudBackgroundColor(color))));
      // A colour-only update and a partial dial redraw must preserve the background.
      v.current+=3;ui.update(v);settle(d);
      const auto pixels=image;lv_obj_invalidate(ui.root());lv_refr_now(d);
      assert(pixels==image);
      v.speedScreen=true;snapshot("hud-background-speed");
      lv_obj_t *speedPanel=nullptr;
      for(uint32_t i=0;i<lv_obj_get_child_count(ui.root());++i) {
        auto *child=lv_obj_get_child(ui.root(),i);
        if(!lv_obj_is_hidden(child) && lv_obj_get_width(child)==width && lv_obj_get_height(child)==height &&
           lv_obj_get_style_bg_opa(child,LV_PART_MAIN)==LV_OPA_COVER)speedPanel=child;
      }
      assert(speedPanel && lv_color_to_u32(lv_obj_get_style_bg_color(speedPanel,LV_PART_MAIN))==
             lv_color_to_u32(lv_color_hex(hudBackgroundColor(color))));
      v.speedScreen=false;
    }
    v.sourceId=100;v.hudBackground=1;v.feedback=false;v.secondaryFeedback=false;
    std::strcpy(v.status,"Нет связи с повороткой");snapshot("hud-background-offline");
    v.hud=false;snapshot("background-local");
    assert(lv_color_to_u32(lv_obj_get_style_bg_color(ui.root(),LV_PART_MAIN))==lv_color_to_u32(lv_color_hex(hudBackgroundColor(0))));
    v=saved;ui.update(v);settle(d);
  }
  const auto idleGeometry=ui.touchLayout();
  // The enlarged caption must have real Cyrillic glyphs, not placeholders.
  const auto checkCaption=[&](auto &&self,lv_obj_t *object)->bool {
    if(lv_obj_check_type(object,&lv_label_class) && !std::strcmp(lv_label_get_text(object),"Цель")) {
      lv_font_glyph_dsc_t glyph{};
      assert(lv_font_get_glyph_dsc(lv_obj_get_style_text_font(object,LV_PART_MAIN),&glyph,0x426,0));
      return true;
    }
    for(uint32_t i=0;i<lv_obj_get_child_count(object);++i) if(self(self,lv_obj_get_child(object,i)))return true;
    return false;
  };
  assert(checkCaption(checkCaption,ui.root()));
  {
    const auto saved=v;
    for(unsigned mask=0;mask<=DisplayAllFields;++mask) {
      v.visible=mask;ui.update(v);settle(d);
      checkCurrentLayout(v,ui.touchLayout());
      assert(lv_obj_is_hidden(findLabel(findLabel,ui.root(),v.hudName))==!bool(mask&DisplayName));
      assert(lv_obj_is_hidden(findLabel(findLabel,ui.root(),v.heap))==!bool(mask&DisplayHeap));
      assert(lv_obj_is_hidden(findLabel(findLabel,ui.root(),v.ip))==!bool(mask&(DisplayLocalAddress|DisplayApAddress)));
      assert(bool(ui.touchLayout().axis.width)==bool(mask&DisplayAxis));
      assert(bool(ui.touchLayout().name.width)==bool(mask&(DisplayName|DisplayControllerAddress)));
      auto *axis=findLabel(findLabel,ui.root(),compact?"AZ":"АЗИМУТ · AZ");assert(axis);
      assert(lv_obj_is_hidden(axis)==!bool(mask&DisplayAxis));
      auto *address=findLabel(findLabel,ui.root(),compact?v.hudAddress:"hud:192.168.3.234 · 0.0°..450.0°");assert(address);
      assert(lv_obj_is_hidden(address)==!bool(mask&DisplayControllerAddress));
      for(bool moving:{true,false}) {
        v.movingNow=moving;std::strcpy(v.status,moving?"Вращение":"");ui.update(v);settle(d);
        checkCurrentLayout(v,ui.touchLayout());
        assert(bool(ui.touchLayout().stop.width)==moving);
        assert(lv_obj_is_hidden(address)==(moving||!bool(mask&DisplayControllerAddress)));
        assert(lv_obj_is_hidden(findLabel(findLabel,ui.root(),v.ip))==(moving||!bool(mask&(DisplayLocalAddress|DisplayApAddress))));
        assert(lv_obj_is_hidden(findLabel(findLabel,ui.root(),v.heap))==(moving||!bool(mask&DisplayHeap)));
        if(!(mask&DisplayName))assert(bool(ui.touchLayout().name.width)==(!moving&&bool(mask&DisplayControllerAddress)));
      }
      const auto pixels=image;lv_obj_invalidate(ui.root());lv_refr_now(d);assert(pixels==image);
    }
    v.visible=DisplayAllFields;snapshot("fields-all");
    v.visible=0;snapshot("fields-none");
    if(compact) {
      v.visible=DisplayAllFields;v.autoMode=false;v.pending=true;
      snapshot("fields-pending");
      v.statusFault=true;std::strcpy(v.status,"Ошибка ввода");snapshot("fields-fault");
      auto *goal=findLabel(findLabel,ui.root(),"EL 80.0°"),*status=findLabel(findLabel,ui.root(),v.status);
      auto *ip=findLabel(findLabel,ui.root(),v.ip),*heap=findLabel(findLabel,ui.root(),v.heap);
      assert(goal&&status&&ip&&heap);
      assert(lv_obj_get_y(goal)+lv_obj_get_height(goal)<=lv_obj_get_y(status));
      assert(lv_obj_get_y(status)+lv_obj_get_height(status)<=lv_obj_get_y(ip));
      assert(lv_obj_get_y(ip)+lv_obj_get_height(ip)<=lv_obj_get_y(heap));
      assert(lv_obj_get_y(heap)+lv_obj_get_height(heap)<=height);
    }
    v=saved;ui.update(v);settle(d);
  }
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
  {
    const auto saved=v;
    for(bool elevation:{false,true})for(bool fromTarget:{false,true}) {
      v.elevation=elevation;v.movingNow=false;ui.update(v);settle(d);
      const auto start=ui.touchLayout();const auto &r=fromTarget?start.target:start.current;
      const int sx=r.x+r.width/2,sy=r.y+r.height/2;
      panel::TouchGesture swipe;swipe.sample(false,0,0,start,0);swipe.sample(true,sx,sy,start,10);
      auto event=swipe.sample(true,sx,sy+start.swipeDeadzone+4,start,20);assert(event.manual&&event.el<0);
      const auto gesture=event.gesture;
      v.movingNow=true;v.elDirection=-1;std::strcpy(v.status,"Вращение");ui.update(v);settle(d);
      const auto moving=ui.touchLayout();assert(moving.stop.width);
      event=press(swipe,moving,moving.stop,30);
      assert(event.manual&&!event.release&&event.gesture==gesture&&event.action==panel::TouchAction::None);
      event=swipe.sample(false,0,0,moving,40);assert(event.manual&&event.release);
      assert(press(swipe,moving,moving.stop,50).action==panel::TouchAction::Stop);
      swipe.sample(false,0,0,moving,60);
      v.movingNow=false;v.elDirection=0;v.status[0]=0;ui.update(v);settle(d);
      assert(!ui.touchLayout().stop.width);
    }
    v=saved;ui.update(v);settle(d);
  }
  {
    const auto saved=v;
    v.hud=false;v.minimum=0;v.maximum=450;v.targetValid=false;v.current=51;
    snapshot("limit-hidden");
    const auto rimPixel=[&](float bearing) {
      const float angle=bearing*3.14159265358979323846f/180.f;
      const int radius=std::lround(217.f*std::min(width,height)/480.f);
      const int x=width/2+std::lround(radius*std::sin(angle));
      const int y=height/2-std::lround(radius*std::cos(angle));
      return image[y*width+x];
    };
    const auto onset=rimPixel(-10),nearTail=rimPixel(-30),pastTail=rimPixel(-75);
    v.current=50;snapshot("limit-threshold");assert(rimPixel(-10)==onset);
    v.current=49;snapshot("limit-visible");assert(rimPixel(-10)!=onset);
    v.current=0;snapshot("limit-tail");
    assert(rimPixel(-30)!=nearTail && rimPixel(-75)==pastTail);
    v=saved;ui.update(v);settle(d);
  }
  if(std::min(width,height)<320) {
    // Hidden diagnostic telemetry must not dirty the compact screen's layout.
    const auto quiet=flushes;v.wifiRssi=-81;std::strcpy(v.heap,"Heap: 120 КБ");
    ui.update(v);settle(d);assert(flushes==quiet);
    v.wifiRssi=-55;std::strcpy(v.heap,"Heap: 123 КБ");ui.update(v);settle(d);
    assert(flushes==quiet);
    const auto saved=v;
    v.minimum=-50;v.maximum=190;v.current=-20.23f;v.target=154;
    ui.update(v);settle(d);
    const auto before=flushedPixels;
    v.current+=.2f;ui.update(v);lv_refr_now(d);
    // A warning-ring update must leave the centre of the screen alone.
    assert(flushedPixels-before<size_t(width*height/4));
    settle(d);
    v.current+=4;ui.update(v);lv_refr_now(d);
    unsigned animatedFrames=0;
    for(unsigned i=0;i<6;++i) {
      const auto step=flushedPixels;lv_tick_inc(LV_DEF_REFR_PERIOD);lv_timer_handler();lv_refr_now(d);
      if(flushedPixels!=step)++animatedFrames;
      assert(flushedPixels-step<size_t(width*height/40));
      const auto pixels=image;lv_obj_invalidate(ui.root());lv_refr_now(d);
      if(pixels!=image) {
        std::snprintf(path,sizeof path,"%s/animation-full.ppm",argv[1]);save(path);
        const auto full=image;image=pixels;std::snprintf(path,sizeof path,"%s/animation-partial.ppm",argv[1]);save(path);image=full;
      }
      assert(pixels==image);
    }
    assert(animatedFrames>=2);
    // Both warning lanes and markers must restore their old pixels, including
    // ranges crossing north and simultaneous lower/upper warning changes.
    v.minimum=-25;v.maximum=155;
    for(unsigned i=0;i<36;++i) {
      v.current=-20+5.f*i;v.target=155-4.f*i;
      ui.update(v);settle(d);
      const auto pixels=image;lv_obj_invalidate(ui.root());lv_refr_now(d);assert(pixels==image);
    }
    v=saved;ui.update(v);settle(d);
  }
  if(!compact) {
    const auto saved=v;
    // The full dashboard keeps its readings and targets fixed while STOP
    // replaces status/telemetry, and restores those fields after release.
    for(bool dual:{false,true})for(bool elevation:{false,true})for(bool pending:{false,true}) {
      v.dualAxis=dual;v.elevation=elevation;v.autoMode=false;v.pending=pending;
      std::strcpy(v.status,"Остановлен");ui.update(v);settle(d);
      std::vector<lv_obj_t *> labels;
      for(const char *text:{"121°","210.0°",pending?"ЕХАТЬ":"Цель","Остановлен","192.168.3.234","Heap: 123 КБ"}) {
        auto *label=findLabel(findLabel,ui.root(),text);assert(label);labels.push_back(label);
      }
      std::vector<const lv_font_t *> fonts;
      std::vector<lv_area_t> areas;
      for(auto *label:labels) {
        fonts.push_back(lv_obj_get_style_text_font(label,LV_PART_MAIN));
        lv_area_t area;lv_obj_get_coords(label,&area);areas.push_back(area);
      }
      for(unsigned state=0;state<6;++state) {
        v.movingNow=state<2;v.statusFault=state==4;v.enabled=state!=5;
        std::strcpy(v.status,state<2?"Вращение":state==2?"":state==3?"Имитация":state==4?"Ошибка ввода":"Остановлен");
        ui.update(v);settle(d);
        assert(bool(ui.touchLayout().stop.width)==v.movingNow);
        for(size_t i=0;i<labels.size();++i) {
          assert(lv_obj_get_style_text_font(labels[i],LV_PART_MAIN)==fonts[i]);
          lv_area_t area;lv_obj_get_coords(labels[i],&area);
          assert(area.x1==areas[i].x1 && area.y1==areas[i].y1);
          // Status has a fixed width but its text can change height when empty.
          if(i!=3)assert(area.x2==areas[i].x2 && area.y2==areas[i].y2);
          assert(lv_obj_is_hidden(labels[i])==(v.movingNow&&i>=3));
        }
      }
      v.movingNow=false;v.statusFault=false;v.enabled=true;
    }
    v=saved;ui.update(v);settle(d);
  }
  if(std::min(width,height)<320) {
    const auto saved=v;
    for(bool elevation:{false,true}) {
      v.autoMode=false;v.pending=v.secondaryPending=false;v.elevation=elevation;
      v.current=107.71f;v.secondaryCurrent=190;v.status[0]=0;
      ui.update(v);settle(d);
      auto *angle=findLabel(findLabel,ui.root(),"108°");assert(angle);
      const auto *idleFont=lv_obj_get_style_text_font(angle,LV_PART_MAIN);
      panel::TouchGesture swipe;const auto start=ui.touchLayout();
      const int sx=start.current.x+start.current.width/2,sy=start.current.y+start.current.height/2;
      swipe.sample(false,0,0,start,0);swipe.sample(true,sx,sy,start,10);
      auto event=swipe.sample(true,sx,sy+30,start,20);assert(event.manual && event.el<0);
      v.movingNow=true;v.elDirection=-1;v.secondaryCurrent=190;std::strcpy(v.status,"Вращение");
      snapshot(elevation?"swipe-el-moving":"swipe-az-moving");
      assert(lv_obj_get_style_text_font(angle,LV_PART_MAIN)->line_height<=idleFont->line_height);
      event=swipe.sample(false,0,0,ui.touchLayout(),30);assert(event.manual && event.release);
      v.movingNow=false;v.elDirection=0;v.status[0]=0;
      snapshot(elevation?"swipe-el-restored":"swipe-az-restored");
      assert(!ui.touchLayout().stop.width);
      assert(lv_obj_get_style_text_font(angle,LV_PART_MAIN)==idleFont);
      const auto quiet=flushes;v.wifiRssi=-82;std::strcpy(v.heap,"Heap: 119 КБ");
      ui.update(v);settle(d);assert(flushes==quiet);
    }
    v=saved;ui.update(v);settle(d);
  }
  const auto targetHeight=lv_obj_get_height(findLabel(findLabel,ui.root(),"210.0°"));
  v.autoMode=false;v.pending=true;snapshot("confirm");
  if(std::min(width,height)<320) {
    assert(lv_obj_get_height(findLabel(findLabel,ui.root(),"210.0°"))>=targetHeight*18/10);
    auto pendingLayout=ui.touchLayout();assert(inside(pendingLayout.target));
    auto *address=findLabel(findLabel,ui.root(),"192.168.3.234");
    lv_area_t addressArea;lv_obj_get_coords(address,&addressArea);assert(addressArea.y2<height);
    panel::TouchGesture confirmation;confirmation.sample(false,0,0,pendingLayout,0);
    const auto down=press(confirmation,pendingLayout,pendingLayout.target,10);assert(!down.manual);
    const auto &r=pendingLayout.target;
    assert(confirmation.sample(false,r.x+r.width/2,r.y+r.height/2,pendingLayout,20).action==panel::TouchAction::Confirm);
    const auto pendingView=v;
    v.pending=false;v.secondaryPending=true;v.secondaryTarget=98.5f;snapshot("confirm-other");
    assert(findLabel(findLabel,ui.root(),"ЕХАТЬ · EL"));
    assert(lv_obj_get_height(findLabel(findLabel,ui.root(),"98.5°"))>=targetHeight*18/10);
    v=pendingView;v.pending=false;snapshot("confirmed");
    assert(lv_obj_get_height(findLabel(findLabel,ui.root(),"210.0°"))==targetHeight);
    const auto saved=v;
    for(int az=-1;az<=1;++az)for(int el=-1;el<=1;++el) {
      if(!az&&!el)continue;
      v.movingNow=true;v.azDirection=az;v.elDirection=el;
      char name[40];std::snprintf(name,sizeof name,"direction-%d-%d",az,el);snapshot(name);
      unsigned visible=0;
      for(uint32_t i=0;i<lv_obj_get_child_count(ui.root());++i) {
        auto *obj=lv_obj_get_child(ui.root(),i);
        if(!lv_obj_check_type(obj,&lv_line_class)||lv_obj_is_hidden(obj))continue;
        ++visible;assert(lv_line_get_point_count(obj)==5);
        const auto *points=lv_line_get_points(obj);
        const int dx=points[1].x-points[4].x,dy=points[1].y-points[4].y;
        assert((dx>0)-(dx<0)==az && (dy>0)-(dy<0)==-el);
      }
      assert(visible==2);
    }
    v=saved;ui.update(v);settle(d);
  }
  v.pending=false;v.movingNow=true;v.azDirection=1;std::strcpy(v.status,"Вращение");snapshot("moving");
  layout=ui.touchLayout();
  if(compact) {
    assert(idleGeometry.current.height>=layout.current.height);
    assert(idleGeometry.target.y>layout.target.y);
    panel::TouchGesture input;
    input.sample(false,0,0,layout,0);assert(press(input,layout,layout.stop,10).action==panel::TouchAction::Stop);
    input.sample(false,0,0,layout,20);assert(press(input,layout,layout.stop,30).action==panel::TouchAction::Stop);
  } else {
    assert(idleGeometry.current.height==layout.current.height);
    assert(idleGeometry.target.y==layout.target.y);
  }
  // STOP remains reachable when ordinary commands are disabled, including the speed page.
  v.enabled=false;v.speedScreen=true;v.speedAvailable[0]=v.speedAvailable[1]=true;
  snapshot("speed-stop");layout=ui.touchLayout();panel::TouchGesture blocked;
  assert(press(blocked,layout,layout.stop,40).action==panel::TouchAction::Stop);
  const auto movingBack=layout.speedBack;
  v.enabled=true;v.movingNow=false;v.azDirection=0;std::strcpy(v.status,"");snapshot("speed");
  layout=ui.touchLayout();panel::TouchGesture speed;
  assert(movingBack.x!=layout.speedBack.x && movingBack.width!=layout.speedBack.width);
  speed.sample(false,0,0,layout,50);
  const auto e=press(speed,layout,layout.speedTracks[1],60);assert(e.action==panel::TouchAction::SpeedValue && e.axis==1);
  v.speedScreen=false;v.hudSelector=true;
  std::strcpy(v.hudRows[0],"Мачта · G-800DXA\n120° · Стоп");std::strcpy(v.hudRows[1],"Дача · G-5500\n121° / 35° · Стоп");std::strcpy(v.hudRows[2],"Антенна · G-800SA\nНет связи");snapshot("menu");
  v.movingNow=true;snapshot("menu-stop");assert(ui.touchLayout().stop.width);v.movingNow=false;
  v.hudSelector=false;v.feedback=v.secondaryFeedback=v.targetValid=v.secondaryTargetValid=false;
  std::strcpy(v.status,"Нет связи с повороткой");snapshot("offline");
  v.hud=false;v.settingsAvailable=DISPLAY_KIND==DISPLAY_ILI9341 && !roundPanel;v.dualAxis=false;v.feedback=true;v.current=359.9;
  v.targetValid=true;v.target=450;v.minimum=0;v.maximum=450;std::strcpy(v.status,"");snapshot("single");
  v.current=-359;v.target=721;v.minimum=-360;v.maximum=1080;snapshot("wide");
  {
    const auto saved=v;
    for(bool dual:{false,true})for(bool elevation:{false,true})for(bool pending:{false,true})for(bool missing:{false,true}) {
      v.dualAxis=dual;v.elevation=elevation;v.pending=pending;v.autoMode=false;
      v.minimum=-3600;v.maximum=3600;v.current=-3600;v.secondaryCurrent=3600;
      v.feedback=v.secondaryFeedback=!missing;v.uncalibrated=v.secondaryUncalibrated=missing;
      ui.update(v);settle(d);checkCurrentLayout(v,ui.touchLayout());
    }
    v=saved;ui.update(v);settle(d);
  }
  // Gradient tails crossing 0 degrees must survive partial redraw clipping.
  v.current=v.minimum+4;v.target=v.maximum-8;snapshot("limits");
  std::strcpy(v.ip,"255.255.255.255  AP: 192.168.4.1");snapshot("network");
  v.statusFault=true;std::strcpy(v.status,"Ошибка AZ: нет изменения положения");snapshot("fault");
  std::printf("PASS %dx%d: native LVGL rendering, buffer bounds, idle redraw, arrows/release, layout-specific STOP and speed controls\n",width,height);
}
