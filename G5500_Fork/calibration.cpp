// G5500 Fork; original by UA1CFM. GPL-3.0.
#include "calibration.hpp"
#include <cstring>
namespace forkrot {
float clampf(float value, float minimum, float maximum) {
  return value < minimum ? minimum : (value > maximum ? maximum : value);
}

float mapCal(int raw, int zeroPoint, int fullScalePoint, float maximumDegrees) {
  if (zeroPoint == fullScalePoint) {
    return 0;
  }
  return clampf((raw - zeroPoint) * maximumDegrees / (float)(fullScalePoint - zeroPoint), 0,
                maximumDegrees);
}

bool calPointsMonotonic(const int *millivolts, uint8_t pointCount) {
  if (pointCount < 2) {
    return false;
  }
  for (uint8_t i = 0; i < pointCount; i++) {
    if (millivolts[i] < 0) {
      return false;
    }
  }
  bool increasing = millivolts[pointCount - 1] > millivolts[0];
  if (millivolts[pointCount - 1] == millivolts[0]) {
    return false;
  }
  for (uint8_t i = 1; i < pointCount; i++) {
    if (increasing && millivolts[i] <= millivolts[i - 1]) {
      return false;
    }
    if (!increasing && millivolts[i] >= millivolts[i - 1]) {
      return false;
    }
  }
  return true;
}

float mapPiecewise(int raw, const int *millivolts, const float *degrees, uint8_t pointCount) {
  if (!calPointsMonotonic(millivolts, pointCount)) {
    return 0.0f;
  }
  bool increasing = millivolts[pointCount - 1] > millivolts[0];
  if ((increasing && raw <= millivolts[0]) || (!increasing && raw >= millivolts[0])) {
    return degrees[0];
  }
  if ((increasing && raw >= millivolts[pointCount - 1]) ||
      (!increasing && raw <= millivolts[pointCount - 1])) {
    return degrees[pointCount - 1];
  }
  for (uint8_t i = 0; i < pointCount - 1; i++) {
    bool inside = increasing ? (raw >= millivolts[i] && raw <= millivolts[i + 1])
                             : (raw <= millivolts[i] && raw >= millivolts[i + 1]);
    if (inside) {
      float fraction = (raw - millivolts[i]) / (float)(millivolts[i + 1] - millivolts[i]);
      return degrees[i] + fraction * (degrees[i + 1] - degrees[i]);
    }
  }
  return degrees[pointCount - 1];
}

bool Calibration::activeAzLinearValid() const {
  if (state_.storageFault) return false;
  if (state_.gsAzMode == AZ_CUSTOM) return state_.custom.initialized && state_.custom.zeroSet &&
      state_.custom.fullSet && std::abs(state_.custom.fullMv - state_.custom.zeroMv) > 100;
  return state_.gsAzMode == 36 ? state_.az36LinearValid : state_.az45LinearValid;
}

bool Calibration::activeAzMultiValid() const {
  if (state_.storageFault) return false;
  if (state_.gsAzMode == AZ_CUSTOM) {
    const auto &c = state_.custom;
    if (!c.initialized || !c.range.valid() || c.count < 2 || c.count > AZ_CUSTOM_CAL_N ||
        c.degrees[0] != 0 || std::abs(c.degrees[c.count-1] - c.range.span()) > .001f) return false;
    for (unsigned i=1; i<c.count; ++i)
      if (!std::isfinite(c.degrees[i]) || c.degrees[i] <= c.degrees[i-1]) return false;
    return calPointsMonotonic(c.millivolts, c.count);
  }
  return state_.gsAzMode == 36 ? state_.az36MultiValid : state_.az45MultiValid;
}

int Calibration::activeAz2Zero() const {
  if (state_.gsAzMode == AZ_CUSTOM) return state_.custom.zeroMv;
  return state_.gsAzMode == 36 ? state_.cal.az36_0 : state_.cal.az45_0;
}

int Calibration::activeAz2Full() const {
  if (state_.gsAzMode == AZ_CUSTOM) return state_.custom.fullMv;
  return state_.gsAzMode == 36 ? state_.cal.az36Max : state_.cal.az45Max;
}

bool Calibration::activeAzOSet() const {
  if (state_.gsAzMode == AZ_CUSTOM) return state_.custom.zeroSet;
  return state_.gsAzMode == 36 ? state_.az36OSet : state_.az45OSet;
}

bool Calibration::activeAzFSet() const {
  if (state_.gsAzMode == AZ_CUSTOM) return state_.custom.fullSet;
  return state_.gsAzMode == 36 ? state_.az36FSet : state_.az45FSet;
}

float Calibration::activeAzMaxDeg() const {
  return activeAzRange().end;
}
AzRange Calibration::activeAzRange() const {
  return state_.gsAzMode == AZ_CUSTOM ? state_.custom.range : AzRange{0, state_.gsAzMode == 36 ? 360.f : 450.f};
}
float Calibration::mapAz(int mv) const {
  const auto r = activeAzRange();
  float travel;
  if (state_.calibrationMode == CAL_MULTI_POINT && activeAzMultiValid()) {
    if (state_.gsAzMode == AZ_CUSTOM) travel = mapPiecewise(mv, state_.custom.millivolts, state_.custom.degrees, state_.custom.count);
    else if (state_.gsAzMode == AZ_P36) travel = mapPiecewise(mv, state_.az36CalMv, az36CalDeg, AZ36_CAL_N);
    else travel = mapPiecewise(mv, state_.az45CalMv, az45CalDeg, AZ45_CAL_N);
  } else if (state_.calibrationMode == CAL_GS232_2POINT && activeAzLinearValid())
    travel = mapCal(mv, activeAz2Zero(), activeAz2Full(), r.span());
  else travel = clampf(mv * r.span() / 3100.f, 0, r.span());
  return r.start + travel;
}
bool Calibration::configureAz(uint8_t profile, float start, float span) {
  AzRange r{start, start + span};
  if (!AzRange::tenth(span) || !validAzProfile(profile, r)) return false;
  if (profile == AZ_CUSTOM) {
    auto &c = state_.custom;
    if (!c.initialized) {
      const bool p36 = state_.gsAzMode == AZ_P36;
      c.range = activeAzRange();
      c.zeroMv = activeAz2Zero(); c.fullMv = activeAz2Full();
      c.zeroSet = activeAzOSet(); c.fullSet = activeAzFSet();
      c.count = p36 ? AZ36_CAL_N : AZ45_CAL_N;
      for (unsigned i=0; i<c.count; ++i) {
        c.degrees[i] = p36 ? az36CalDeg[i] : az45CalDeg[i];
        c.millivolts[i] = p36 ? state_.az36CalMv[i] : state_.az45CalMv[i];
      }
      c.initialized = true;
    }
    if (std::abs(c.range.span() - span) > .001f) {
      c.zeroSet = c.fullSet = false;
      c.count = 2; c.degrees[0] = 0; c.degrees[1] = span;
      std::fill_n(c.millivolts, AZ_CUSTOM_CAL_N, -1);
    }
    c.range = r;
  }
  state_.gsAzMode = profile;
  return true;
}
static bool editPoint(CustomAxisCalibration &c, const char *op, float angle, int mv) {
  if (!c.initialized || !AzRange::tenth(angle) || !c.range.contains(angle)) return false;
  const float distance = std::round((angle - c.range.start) * 10) / 10;
  unsigned i=0;
  while (i<c.count && c.degrees[i] < distance - .001f) ++i;
  const bool exists = i<c.count && std::abs(c.degrees[i] - distance) < .001f;
  if (std::strcmp(op, "add") == 0) {
    if (exists || c.count == AZ_CUSTOM_CAL_N) return false;
    for (unsigned j=c.count; j>i; --j) { c.degrees[j]=c.degrees[j-1]; c.millivolts[j]=c.millivolts[j-1]; }
    c.degrees[i]=distance; c.millivolts[i]=-1; ++c.count;
  } else if (std::strcmp(op, "capture") == 0) {
    if (!exists || mv<0) return false;
    c.millivolts[i]=mv;
  } else if (std::strcmp(op, "delete") == 0) {
    if (!exists || i==0 || i+1==c.count) return false;
    for (unsigned j=i; j+1<c.count; ++j) { c.degrees[j]=c.degrees[j+1]; c.millivolts[j]=c.millivolts[j+1]; }
    --c.count;
  } else return false;
  return true;
}
bool Calibration::editAzPoint(const char *op, float angle, int mv) {
  return state_.gsAzMode == AZ_CUSTOM && editPoint(state_.custom, op, angle, mv);
}
bool Calibration::editElPoint(const char *op, float angle, int mv) {
  return state_.elProfile == EL_CUSTOM && editPoint(state_.customEl, op, angle, mv);
}
void Calibration::captureAzEndpoint(bool full, int mv) {
  if (state_.gsAzMode == AZ_CUSTOM) {
    (full ? state_.custom.fullMv : state_.custom.zeroMv) = mv;
    (full ? state_.custom.fullSet : state_.custom.zeroSet) = true;
  } else if (state_.gsAzMode == AZ_P36) {
    (full ? state_.cal.az36Max : state_.cal.az36_0) = mv;
    (full ? state_.az36FSet : state_.az36OSet) = true;
  } else {
    (full ? state_.cal.az45Max : state_.cal.az45_0) = mv;
    (full ? state_.az45FSet : state_.az45OSet) = true;
  }
}
void Calibration::resetAz(bool multi) {
  state_.storageFault = false;
  if (state_.gsAzMode == AZ_CUSTOM) {
    if (multi) std::fill_n(state_.custom.millivolts, AZ_CUSTOM_CAL_N, -1);
    else state_.custom.zeroSet = state_.custom.fullSet = false;
  } else if (state_.gsAzMode == AZ_P36) {
    if (multi) std::fill_n(state_.az36CalMv, AZ36_CAL_N, -1);
    else state_.az36OSet = state_.az36FSet = state_.az36LinearValid = false;
  } else {
    if (multi) std::fill_n(state_.az45CalMv, AZ45_CAL_N, -1);
    else state_.az45OSet = state_.az45FSet = state_.az45LinearValid = false;
  }
  refreshMultiCalValidity();
}

bool Calibration::activeElLinearValid() const {
  if (state_.storageFault) return false;
  const auto &c=state_.customEl;
  return state_.elProfile == EL_CUSTOM ? c.initialized && c.zeroSet && c.fullSet &&
      std::abs(c.fullMv-c.zeroMv)>100 : state_.elLinearValid;
}
bool Calibration::activeElMultiValid() const {
  if (state_.storageFault) return false;
  if (state_.elProfile == EL_STANDARD) return state_.elMultiValid;
  const auto &c=state_.customEl;
  if (!c.initialized || !c.range.valid() || c.count<2 || c.count>AZ_CUSTOM_CAL_N ||
      c.degrees[0]!=0 || std::abs(c.degrees[c.count-1]-c.range.span())>.001f) return false;
  for(unsigned i=1;i<c.count;++i)
    if(!std::isfinite(c.degrees[i]) || c.degrees[i]<=c.degrees[i-1]) return false;
  return calPointsMonotonic(c.millivolts,c.count);
}
int Calibration::activeEl2Zero() const { return state_.elProfile==EL_CUSTOM ? state_.customEl.zeroMv : state_.cal.el0; }
int Calibration::activeEl2Full() const { return state_.elProfile==EL_CUSTOM ? state_.customEl.fullMv : state_.cal.elMax; }
bool Calibration::activeElOSet() const { return state_.elProfile==EL_CUSTOM ? state_.customEl.zeroSet : state_.elOSet; }
bool Calibration::activeElFSet() const { return state_.elProfile==EL_CUSTOM ? state_.customEl.fullSet : state_.elFSet; }
RotationRange Calibration::activeElRange() const { return state_.elProfile==EL_CUSTOM ? state_.customEl.range : RotationRange{0,180}; }
float Calibration::mapEl(int mv) const {
  const auto r=activeElRange(); float travel;
  if(state_.calibrationMode==CAL_MULTI_POINT && activeElMultiValid()) {
    const auto &c=state_.customEl;
    travel=state_.elProfile==EL_CUSTOM ? mapPiecewise(mv,c.millivolts,c.degrees,c.count) : mapPiecewise(mv,state_.elCalMv,elCalDeg,EL_CAL_N);
  } else if(state_.calibrationMode==CAL_GS232_2POINT && activeElLinearValid())
    travel=mapCal(mv,activeEl2Zero(),activeEl2Full(),r.span());
  else travel=clampf(mv*r.span()/3100.f,0,r.span());
  return r.start+travel;
}
bool Calibration::configureEl(uint8_t profile, float start, float span) {
  RotationRange r{start,start+span};
  if(controller_.model!=Model::G5500 || !RotationRange::tenth(span) || !validElProfile(profile,r)) return false;
  if(profile==EL_CUSTOM) {
    auto &c=state_.customEl;
    if(!c.initialized) {
      c.range={0,180};c.zeroMv=state_.cal.el0;c.fullMv=state_.cal.elMax;
      c.zeroSet=state_.elOSet;c.fullSet=state_.elFSet;c.count=EL_CAL_N;
      for(unsigned i=0;i<c.count;++i){c.degrees[i]=elCalDeg[i];c.millivolts[i]=state_.elCalMv[i];}
      c.initialized=true;
    }
    if(std::abs(c.range.span()-span)>.001f) {
      c.zeroSet=c.fullSet=false;c.count=2;c.degrees[0]=0;c.degrees[1]=span;
      std::fill_n(c.millivolts,AZ_CUSTOM_CAL_N,-1);
    }
    c.range=r;
  }
  state_.elProfile=profile;return true;
}
void Calibration::captureElEndpoint(bool full,int mv) {
  if(state_.elProfile==EL_CUSTOM) {
    (full?state_.customEl.fullMv:state_.customEl.zeroMv)=mv;
    (full?state_.customEl.fullSet:state_.customEl.zeroSet)=true;
  } else {
    (full?state_.cal.elMax:state_.cal.el0)=mv;
    (full?state_.elFSet:state_.elOSet)=true;
  }
}
void Calibration::resetEl(bool multi) {
  state_.storageFault=false;
  if(state_.elProfile==EL_CUSTOM) {
    if(multi)std::fill_n(state_.customEl.millivolts,AZ_CUSTOM_CAL_N,-1);
    else state_.customEl.zeroSet=state_.customEl.fullSet=false;
  } else {
    if(multi)std::fill_n(state_.elCalMv,EL_CAL_N,-1);
    else state_.elOSet=state_.elFSet=state_.elLinearValid=false;
  }
  refreshMultiCalValidity();
}
bool Calibration::writeElJson(JsonWriter &j) const {
  const auto &s=state_;const auto r=activeElRange();const auto &c=s.customEl;
  const bool custom=s.elProfile==EL_CUSTOM;
  j << "{\"profile\":" << jsonQuoted(elProfileName(s.elProfile)) << ",\"start\":" << r.start
    << ",\"span\":" << r.span() << ",\"end\":" << r.end
    << ",\"storageFault\":" << (s.storageFault?"true":"false")
    << ",\"customStart\":" << (c.initialized?c.range.start:r.start)
    << ",\"customSpan\":" << (c.initialized?c.range.span():r.span()) << ",\"points\":[";
  const unsigned n=custom?c.count:EL_CAL_N;
  for(unsigned i=0;i<n;++i) {
    if(i)j<<',';
    j << "{\"angle\":" << (custom?r.start+c.degrees[i]:elCalDeg[i])
      << ",\"mv\":" << (custom?c.millivolts[i]:s.elCalMv[i])
      << ",\"endpoint\":" << (i==0||i+1==n?"true":"false") << '}';
  }
  j<<"]}";return j.good();
}

void Calibration::refreshMultiCalValidity() {
  state_.az36MultiValid = calPointsMonotonic(state_.az36CalMv, AZ36_CAL_N);
  state_.az45MultiValid = calPointsMonotonic(state_.az45CalMv, AZ45_CAL_N);
  state_.elMultiValid = calPointsMonotonic(state_.elCalMv, EL_CAL_N);
}

bool Calibration::selectedCalibrationValid() const {
  bool azOk =
      state_.calibrationMode == CAL_MULTI_POINT ? activeAzMultiValid() : activeAzLinearValid();
  bool elOk =
      state_.calibrationMode == CAL_MULTI_POINT ? activeElMultiValid() : activeElLinearValid();
  return azOk && (controller_.model == forkrot::Model::G800DXA || elOk);
}

const char *Calibration::calibrationModeName() const {
  return state_.calibrationMode == CAL_MULTI_POINT ? "MULTI-POINT" : "GS232 2-POINT";
}
bool Calibration::writeAzJson(JsonWriter &j) const {
    const auto &s=state_; const auto r=activeAzRange();
    const bool custom=s.gsAzMode==AZ_CUSTOM;
    j << "{\"profile\":" << jsonQuoted(azProfileName(s.gsAzMode)) << ",\"start\":" << r.start
      << ",\"span\":" << r.span() << ",\"end\":" << r.end
      << ",\"storageFault\":" << (s.storageFault?"true":"false")
      << ",\"customStart\":" << (s.custom.initialized?s.custom.range.start:r.start)
      << ",\"customSpan\":" << (s.custom.initialized?s.custom.range.span():r.span()) << ",\"points\":[";
    const unsigned n=custom?s.custom.count:s.gsAzMode==36?AZ36_CAL_N:AZ45_CAL_N;
    for (unsigned i=0;i<n;++i) {
      if(i)j<<',';
      const float angle=custom?r.start+s.custom.degrees[i]:s.gsAzMode==36?az36CalDeg[i]:az45CalDeg[i];
      const int mv=custom?s.custom.millivolts[i]:s.gsAzMode==36?s.az36CalMv[i]:s.az45CalMv[i];
      j << "{\"angle\":" << angle << ",\"mv\":" << mv << ",\"endpoint\":" << (i==0||i+1==n?"true":"false") << '}';
    }
    j<<"]}"; return j.good();
}
} // namespace forkrot
