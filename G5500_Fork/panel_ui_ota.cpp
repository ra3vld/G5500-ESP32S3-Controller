// An opaque OTA screen also supplies the monochrome SRAM scanout on CrowPanel.
#include "panel_ui.hpp"
#if GRAPHICAL_DISPLAY
#include <cstdio>
#include <cstring>
LV_FONT_DECLARE(panel_font_18)
LV_FONT_DECLARE(panel_font_24)
LV_FONT_DECLARE(panel_font_36)
LV_FONT_DECLARE(panel_font_40)
LV_FONT_DECLARE(panel_font_48)
LV_FONT_DECLARE(panel_font_56)
LV_FONT_DECLARE(panel_font_64)
LV_FONT_DECLARE(panel_font_72)
LV_FONT_DECLARE(panel_font_80)
LV_FONT_DECLARE(panel_font_96)
LV_FONT_DECLARE(panel_font_28)
LV_FONT_DECLARE(panel_font_14)
namespace forkrot {
void PanelUi::showOta(const OtaProgress &progress) {
  if(!root_)return;
  if(!otaPanel_) {
    otaPanel_=lv_obj_create(root_);lv_obj_remove_style_all(otaPanel_);
    lv_obj_set_size(otaPanel_,width_,height_);lv_obj_set_pos(otaPanel_,0,0);
    lv_obj_set_style_bg_color(otaPanel_,lv_color_black(),0);
    lv_obj_set_style_bg_opa(otaPanel_,LV_OPA_COVER,0);
    lv_obj_set_scrollable(otaPanel_,false);
    auto make=[&](int top,int size) {
      auto *obj=lv_label_create(otaPanel_);
      lv_obj_set_width(obj,p(440));lv_obj_set_pos(obj,(width_-p(440))/2,y(top));
      lv_obj_set_style_text_align(obj,LV_TEXT_ALIGN_CENTER,0);
      lv_obj_set_style_text_color(obj,lv_color_white(),0);
      lv_obj_set_style_text_font(obj,face(size),0);
      lv_label_set_long_mode(obj,LV_LABEL_LONG_WRAP);return obj;
    };
    otaStage_=make(78,28);otaWarning_=make(124,22);
    otaPercent_=make(250,112);otaSign_=make(250,28);otaDone_=make(394,28);
    const auto *caption=diameter_>=320?&panel_font_28:&panel_font_14;
    lv_obj_set_style_text_font(otaStage_,caption,0);lv_obj_set_style_text_font(otaDone_,caption,0);
    // Reuse the actual current-position face, including its adaptive size.
    otaFont_=*lv_obj_get_style_text_font(angle_,LV_PART_MAIN);
    lv_obj_set_style_text_font(otaPercent_,&otaFont_,0);
    lv_label_set_long_mode(otaPercent_,LV_LABEL_LONG_CLIP);
    lv_obj_set_width(otaPercent_,LV_SIZE_CONTENT);
    // Match the percent glyph to 60% of the digits' visible height, rather
    // than their font's nominal size or unused line padding.
    lv_font_glyph_dsc_t digit{},sign{};
    lv_font_get_glyph_dsc(&otaFont_,&digit,'0',0);
    int best=10000;
    for(const auto *font:{&panel_font_14,&panel_font_18,&panel_font_24,&panel_font_28,
                         &panel_font_36,&panel_font_40,&panel_font_48,&panel_font_56,&panel_font_64,
                         &panel_font_72,&panel_font_80,&panel_font_96}) {
      lv_font_glyph_dsc_t glyph{};lv_font_get_glyph_dsc(font,&glyph,'%',0);
      int delta=int(glyph.box_h)*5-int(digit.box_h)*3;if(delta<0)delta=-delta;
      if(delta<best) {best=delta;otaSignFont_=*font;sign=glyph;}
    }
    otaSignFont_.line_height=sign.box_h;otaSignFont_.base_line=-sign.ofs_y;
    lv_obj_set_style_text_font(otaSign_,&otaSignFont_,0);
    lv_obj_set_width(otaSign_,LV_SIZE_CONTENT);lv_label_set_text(otaSign_,"%");
  }
  const auto set=[](lv_obj_t *obj,const char *text) {
    if(std::strcmp(lv_label_get_text(obj),text))lv_label_set_text(obj,text);
  };
  char percent[8];std::snprintf(percent,sizeof percent,"%u",unsigned(progress.percent));
  set(otaStage_,otaStageText(progress.stage));set(otaPercent_,percent);
  lv_obj_update_layout(otaPanel_);
  const int numberWidth=lv_obj_get_width(otaPercent_);
  const int left=(width_-numberWidth-p(4)-lv_obj_get_width(otaSign_))/2;
  lv_obj_set_pos(otaPercent_,left,y(250));
  lv_obj_set_pos(otaSign_,left+numberWidth+p(4),y(250)+otaFont_.line_height-otaFont_.base_line
                 -otaSignFont_.line_height+otaSignFont_.base_line);
  const bool failed=progress.stage==OtaStage::Error;
  set(otaWarning_,failed?"Перезагрузите устройство":"Не перезагружайте\nи не выключайте.");
  set(otaDone_,failed?"загрузка прервана":"выполнено");
  touchLayout_={};touchLayout_.enabled=false;touchLayout_.menuAccess=false;confirmationVisible_=false;
  lv_obj_move_foreground(otaPanel_);
}
}
#endif
