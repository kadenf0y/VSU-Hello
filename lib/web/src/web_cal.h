// cal_store.h
#pragma once
#include <vector>
#include <Arduino.h>

struct CalPoint {
  float raw;      // sensor raw (e.g., ADC-derived mmHg_raw)
  float actual;   // user-entered reference pressure (mmHg)
  uint32_t ms;    // millis() when captured
};

struct CalFit {
  float slope = 1.f;  // actual ≈ slope*raw + offset
  float offset = 0.f;
  float r2 = 0.f;
  int   n = 0;
};

class CalStore {
public:
  std::vector<CalPoint> atr, vent;

  void clear(const String& ch){
    (ch=="atr"? atr: vent).clear();
  }
  void add(const String& ch, float raw, float actual){
    auto &vec = (ch=="atr"? atr: vent);
    vec.push_back({raw, actual, millis()});
  }
  CalFit fit(const String& ch) const{
    const auto &v = (ch=="atr"? atr: vent);
    CalFit f; f.n = (int)v.size();
    if (v.size() < 2) return f;

    double sx=0, sy=0, sxx=0, sxy=0;
    for (auto &p: v){ sx+=p.raw; sy+=p.actual; sxx+=p.raw*p.raw; sxy+=p.raw*p.actual; }
    const double n = (double)v.size();
    const double denom = (n*sxx - sx*sx);
    if (denom == 0) return f;
    f.slope  = (float)((n*sxy - sx*sy) / denom);
    f.offset = (float)((sy - f.slope*sx)/n);

    // r^2
    double meanY = sy/n, ssTot=0, ssRes=0;
    for (auto &p: v){
      double yhat = f.slope*p.raw + f.offset;
      ssRes += (p.actual - yhat)*(p.actual - yhat);
      ssTot += (p.actual - meanY)*(p.actual - meanY);
    }
    f.r2 = ssTot>0 ? (float)(1.0 - ssRes/ssTot) : 0.f;
    return f;
  }
};
