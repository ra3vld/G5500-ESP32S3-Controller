// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include "panel_view.hpp"
#include <algorithm>

namespace forkrot::panel {
// Presentation only: never feed these values back into motion or hit testing.
class Presentation {
  struct Angle {
    bool valid=false;
    uint32_t at=0;
    float filtered=0, position=0, degrees=0;
    void update(float raw,bool feedback,bool moving,uint32_t now) {
      if(!feedback || !std::isfinite(raw)) { valid=false;position=degrees=raw;return; }
      if(!valid || moving) {
        filtered=position=raw;degrees=std::round(raw);
      } else {
        // A two-second time constant, independent of display refresh rate.
        filtered+=(raw-filtered)*(-std::expm1(-float(uint32_t(now-at))/2000.f));
        if(std::abs(filtered-position)>=.1f)position=filtered;
        // Retain the last integer through noise around a rounding boundary.
        if(std::abs(filtered-degrees)>=.7f)degrees=std::round(filtered);
      }
      valid=true;at=now;
    }
  } angles_[2];
  struct Goal {
    float target=0,tolerance=0;
    bool valid=false;
    bool operator==(const Goal &g) const {
      return valid==g.valid && (!valid || (target==g.target && tolerance==g.tolerance));
    }
  } goals_[2];
  uint64_t source_=0;
  bool initialized_=false,hud_=false,dual_=false;
  bool reached_=false,arriving_=false,leaving_=false;
  uint32_t arrivalAt_=0,departureAt_=0;
public:
  struct Reading {
    float position=0,degrees=0;
    bool operator==(const Reading &r) const {return position==r.position && degrees==r.degrees;}
  };
  struct Result {
    Reading current,secondary;
    bool reached=false;
    bool operator==(const Result &r) const {
      return current==r.current && secondary==r.secondary && reached==r.reached;
    }
  };
  Result update(const PanelView &v,uint32_t now) {
    const bool context=!initialized_ || source_!=v.sourceId || hud_!=v.hud || dual_!=v.dualAxis;
    if(context) {
      angles_[0]={};angles_[1]={};
      source_=v.sourceId;hud_=v.hud;dual_=v.dualAxis;initialized_=true;
    }
    const unsigned primary=v.elevation?1:0,other=1-primary;
    const bool moving=v.movingNow || v.direction || v.azDirection || v.elDirection;
    angles_[primary].update(v.current,v.feedback,moving,now);
    angles_[other].update(v.secondaryCurrent,v.dualAxis && v.secondaryFeedback,moving,now);
    Goal goals[2];
    goals[primary]={v.target,v.tolerance,v.targetValid};
    goals[other]={v.secondaryTarget,v.secondaryTolerance,v.dualAxis && v.secondaryTargetValid};
    if(context || !(goals[0]==goals_[0]) || !(goals[1]==goals_[1]))
      reached_=arriving_=leaving_=false;
    goals_[0]=goals[0];goals_[1]=goals[1];
    const float raw[2]={v.elevation?v.secondaryCurrent:v.current,v.elevation?v.current:v.secondaryCurrent};
    const bool feedback[2]={v.elevation?v.secondaryFeedback:v.feedback,v.elevation?v.feedback:v.secondaryFeedback};
    bool usable=!moving && (goals[0].valid || goals[1].valid),within=true;
    for(unsigned i=0;i<2;++i)if(goals[i].valid) {
      const auto &g=goals[i];
      usable=usable && feedback[i] && std::isfinite(raw[i]) && std::isfinite(g.target) &&
          std::isfinite(g.tolerance) && g.tolerance>=0;
      // Small display hysteresis around the real drive tolerance, in degrees.
      const float margin=std::clamp(g.tolerance*.15f,.35f,1.f);
      within=within && std::abs(raw[i]-g.target)<=g.tolerance+margin;
    }
    if(!usable)reached_=arriving_=leaving_=false;
    else if(!reached_) {
      if(!within)arriving_=false;
      else if(!arriving_ && v.targetReached()) {arriving_=true;arrivalAt_=now;}
      else if(arriving_ && uint32_t(now-arrivalAt_)>=500) {reached_=true;leaving_=false;}
    } else if(within)leaving_=false;
    else if(!leaving_) {leaving_=true;departureAt_=now;}
    else if(uint32_t(now-departureAt_)>=1500)reached_=arriving_=leaving_=false;
    return {{angles_[primary].position,angles_[primary].degrees},
            {angles_[other].position,angles_[other].degrees},reached_};
  }
};
}
