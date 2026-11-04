#include "panel_ui.hpp"
#include "crowpanel_ota_bitmap.h"
#include <vector>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace forkrot;
struct Preview {
  unsigned width,height;
  std::vector<uint16_t> stripe,image;
  lv_display_t *display;
  PanelUi ui;
  static void flush(lv_display_t *d,const lv_area_t *area,uint8_t *data) {
    auto &p=*static_cast<Preview *>(lv_display_get_user_data(d));
    auto *pixels=reinterpret_cast<uint16_t *>(data);
    for(int y=area->y1;y<=area->y2;++y)for(int x=area->x1;x<=area->x2;++x)p.image[y*p.width+x]=*pixels++;
    lv_display_flush_ready(d);
  }
  Preview(unsigned w,unsigned h,bool round):width(w),height(h),stripe(w*8),image(w*h) {
    display=lv_display_create(w,h);lv_display_set_default(display);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_user_data(display,this);lv_display_set_flush_cb(display,flush);
    lv_display_set_buffers(display,stripe.data(),nullptr,stripe.size()*2,LV_DISPLAY_RENDER_MODE_PARTIAL);
    ui.create(round);PanelView v;v.feedback=true;v.current=127;v.targetValid=true;v.target=190;
    v.dualAxis=true;v.secondaryFeedback=true;v.secondaryCurrent=20;v.secondaryTargetValid=true;v.secondaryTarget=40;
    ui.update(v);lv_refr_now(display);
  }
  ~Preview(){lv_display_delete(display);}
  static lv_obj_t *find(lv_obj_t *root,const char *text) {
    if(lv_obj_check_type(root,&lv_label_class)&&!std::strcmp(lv_label_get_text(root),text))return root;
    for(unsigned i=0;i<lv_obj_get_child_count(root);++i)if(auto *p=find(lv_obj_get_child(root,i),text))return p;
    return nullptr;
  }
  void check(OtaProgress progress,const char *dir) {
    ui.showOta(progress);lv_refr_now(display);
    char number[8];std::snprintf(number,sizeof number,"%u",unsigned(progress.percent));
    auto *label=find(ui.root(),number);assert(label);
    auto *face=lv_obj_get_style_text_font(label,LV_PART_MAIN);
    auto *sign=find(ui.root(),"%");assert(sign);
    const auto *signFont=lv_obj_get_style_text_font(sign,LV_PART_MAIN);
    lv_font_glyph_dsc_t digit{},glyph{};
    assert(lv_font_get_glyph_dsc(face,&digit,'0',0));
    assert(lv_font_get_glyph_dsc(signFont,&glyph,'%',0)&&glyph.box_w>0&&glyph.box_h>0);
    if(!(glyph.box_h*100>=digit.box_h*50&&glyph.box_h*100<=digit.box_h*68))
      std::fprintf(stderr,"OTA %ux%u: digit height %u, percent height %u\n",width,height,digit.box_h,glyph.box_h);
    assert(glyph.box_h*100>=digit.box_h*50&&glyph.box_h*100<=digit.box_h*68);
    lv_point_t size;lv_text_get_size(&size,number,face,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
    assert(size.x<=lv_obj_get_width(label));
    auto *stage=find(ui.root(),otaStageText(progress.stage));assert(stage);
    assert(ui.touchLayout().enabled==false && ui.touchLayout().menuAccess==false);
    auto *done=find(ui.root(),progress.stage==OtaStage::Error?"загрузка прервана":"выполнено");assert(done);
    auto *warning=find(ui.root(),progress.stage==OtaStage::Error?"Перезагрузите устройство":"Не перезагружайте\nи не выключайте.");assert(warning);
    for(auto *object:{stage,done,warning}) {
      const auto *font=lv_obj_get_style_text_font(object,LV_PART_MAIN);
      const auto *text=reinterpret_cast<const unsigned char *>(lv_label_get_text(object));
      while(*text) {
        uint32_t code=*text++;
        if(code>=0xc0) {code&=0x1f;code=(code<<6)|(*text++&0x3f);}
        if(code==' '||code=='\n')continue;
        lv_font_glyph_dsc_t d{};assert(lv_font_get_glyph_dsc(font,&d,code,0)&&d.box_w&&d.box_h);
      }
    }
    lv_area_t a,b,c;lv_obj_get_coords(stage,&a);lv_obj_get_coords(label,&b);lv_obj_get_coords(done,&c);
    lv_area_t signArea;lv_obj_get_coords(sign,&signArea);
    assert(a.y2<b.y1&&b.y2<c.y1&&c.y2<int(height));
    assert(signArea.x1>b.x2&&signArea.x2<int(width)&&signArea.y1>=b.y1&&signArea.y2<=b.y2+1);
    assert(b.y1>=(int(height)-int(width<height?width:height))/2+250*int(width<height?width:height)/480);
    std::vector<uint8_t> mono(image.size()/8);std::vector<uint16_t> expanded(image.size());
    crowpanel_ota_pack(mono.data(),image.data(),image.size());crowpanel_ota_expand(expanded.data(),mono.data(),image.size());
    unsigned lit=0;for(auto p:expanded)lit+=bool(p);assert(lit>100&&lit<image.size()/2);
    char name[512];std::snprintf(name,sizeof name,"%s/ota-%ux%u-stage%u-%u.ppm",dir,width,height,unsigned(progress.stage),unsigned(progress.percent));
    auto *file=std::fopen(name,"wb");assert(file);std::fprintf(file,"P6\n%u %u\n255\n",width,height);
    for(auto p:expanded){uint8_t rgb[3]={uint8_t(p?255:0),uint8_t(p?255:0),uint8_t(p?255:0)};std::fwrite(rgb,1,3,file);}std::fclose(file);
  }
};
int main(int argc,char **argv) {
  assert(argc==2);lv_init();
  for(auto shape:{std::pair{480,480},std::pair{240,240},std::pair{320,240},std::pair{240,320}}) {
    Preview p(shape.first,shape.second,shape.first==shape.second);
    for(auto state:{OtaProgress{OtaStage::Preparing,0},OtaProgress{OtaStage::Writing,19},
                   OtaProgress{OtaStage::Verifying,99},OtaProgress{OtaStage::Complete,100},OtaProgress{OtaStage::Error,19}})p.check(state,argv[1]);
    std::printf("PASS OTA %dx%d: stages, 0/19/99/100%%, failure, glyphs/bounds, disabled input, SRAM snapshot\n",shape.first,shape.second);
  }
}
