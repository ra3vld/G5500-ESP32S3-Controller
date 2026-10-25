// Render the production ILI9341 calibration pages and exercise its coordinate map.
#include "display.hpp"
#include "calibration.hpp"
#include "storage.hpp"
#include "hardware.hpp"
#include "device_actions.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
#include <type_traits>
namespace forkrot {
class Diagnostics {};
class Network {};
int Hardware::positionValue(unsigned axis) {return axis?1600:1200;}
}
using namespace forkrot;
// Explicit instantiation grants preview access without changing production visibility.
template<class Tag, auto Member> struct Access {
  friend auto member(Tag) {return Member;}
};
#define ACCESS(name,...) struct name##Tag {friend auto member(name##Tag);}; \
  template struct Access<name##Tag,&Display::name>;
ACCESS(calScreen_,lv_obj_t *Display::*)
ACCESS(drawCalHome,void(Display::*)())
ACCESS(drawCalAxis,void(Display::*)(bool))
ACCESS(drawTouchCalibration,void(Display::*)())
ACCESS(drawTftPage,void(Display::*)())
ACCESS(tftPage,uint8_t Display::*)
ACCESS(touchCalStep,uint8_t Display::*)
ACCESS(touchCalibration_,TouchCalibration Display::*)
ACCESS(touch,std::optional<XPT2046_Touchscreen> Display::*)
ACCESS(touchCalRawX,int(Display::*)[4])
ACCESS(touchCalRawY,int(Display::*)[4])
ACCESS(finishTouchCalibration,void(Display::*)())
ACCESS(readTouchXY,bool(Display::*)(int &,int &))
#undef ACCESS
struct Actions : DeviceActions {
  void updatePosition() override {} void setCalibrationMode(CalibrationMode) override {}
  void setGsAzMode(uint8_t) override {} void captureGs232Cal(const String &) override {}
  bool saveCal() override {return true;} bool configureAzRange(uint8_t,float,float) override {return true;}
  bool editAzPoint(const char *,float) override {return true;} bool configureElRange(uint8_t,float,float) override {return true;}
  bool editElPoint(const char *,float) override {return true;} void loadPrefs() override {}
  void loadMotionSettings() override {} void loadProfilePresets() override {} void saveProfilePresets() override {}
  void saveForkSettings() override {} void setupWeb() override {} void invalidateDisplay() override {}
};
static uint16_t pixels[320*240],buffer[320*16];
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *data) {
  auto *p=reinterpret_cast<uint16_t *>(data);
  assert(a->x1>=0 && a->y1>=0 && a->x2<320 && a->y2<240);
  for(int y=a->y1;y<=a->y2;++y)for(int x=a->x1;x<=a->x2;++x)pixels[y*320+x]=*p++;
  lv_display_flush_ready(d);
}
int main(int argc,char **argv) {
  assert(argc==2);lv_init();auto *screen=lv_display_create(320,240);
  lv_display_set_color_format(screen,LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(screen,buffer,nullptr,sizeof buffer,LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(screen,flush);
  Runtime runtime;Calibration calibration(runtime.controller);Storage storage;Hardware hardware(runtime);
  Diagnostics diagnostics;Network network;Actions actions;
  Display ui(runtime,actions,calibration,hardware,diagnostics,network,storage);
  // Use the production screen initialization as well as the page widgets.
  auto &page=ui.*member(tftPageTag{});
  page=static_cast<std::remove_reference_t<decltype(page)>>(1);
  (ui.*member(drawTftPageTag{}))();
  auto save=[&](const char *name) {
    lv_tick_inc(250);lv_timer_handler();lv_refr_now(screen);
    const std::vector<uint16_t> frame(pixels,pixels+320*240);
    lv_obj_invalidate(lv_screen_active());lv_refr_now(screen);
    assert(std::equal(frame.begin(),frame.end(),pixels));
    char path[512];std::snprintf(path,sizeof path,"%s/cal-%s.ppm",argv[1],name);
    auto *f=std::fopen(path,"wb");assert(f);std::fprintf(f,"P6\n320 240\n255\n");
    for(auto c:pixels) {unsigned char b[]={uint8_t(((c>>11)&31)*255/31),uint8_t(((c>>5)&63)*255/63),uint8_t((c&31)*255/31)};std::fwrite(b,1,3,f);}std::fclose(f);
  };
  (ui.*member(drawCalHomeTag{}))();save("home");
  calibration.state().calibrationMode=CAL_GS232_2POINT;
  (ui.*member(drawCalAxisTag{}))(true);save("az-two");
  calibration.state().calibrationMode=CAL_MULTI_POINT;
  (ui.*member(drawCalAxisTag{}))(false);save("el-multi");
  for(unsigned i=0;i<4;++i) {ui.*member(touchCalStepTag{})=i;(ui.*member(drawTouchCalibrationTag{}))();char name[20];std::snprintf(name,sizeof name,"touch-%u",i);save(name);}
  auto &touch=(ui.*member(touchTag{})).emplace();touch.down=true;
  for(bool swap:{false,true}) {
    const int xs[]{400,3600,3600,400},ys[]{500,500,3500,3500};
    for(unsigned i=0;i<4;++i) {
      (ui.*member(touchCalRawXTag{}))[i]=swap?ys[i]:xs[i];
      (ui.*member(touchCalRawYTag{}))[i]=swap?xs[i]:ys[i];
    }
    (ui.*member(finishTouchCalibrationTag{}))();
    const auto &cal=ui.*member(touchCalibration_Tag{});assert(cal.touchCalValid && cal.touchSwapXY==swap);
    TouchCalibration loaded;storage.loadTouchCalibration(loaded);assert(loaded.touchCalValid && loaded.touchSwapXY==swap);
    for(unsigned i=0;i<4;++i) {
      touch.point={swap?ys[i]:xs[i],swap?xs[i]:ys[i],500};int x=0,y=0;
      assert((ui.*member(readTouchXYTag{}))(x,y));assert(x==(i==0||i==3?20:299));assert(y==(i<2?20:219));
    }
  }
  ui.*member(touchCalStepTag{})=4;(ui.*member(drawTouchCalibrationTag{}))();save("saved");
  std::puts("PASS: production ILI9341 calibration pages, four-corner mapping, swapped axes and persisted calibration");
}
