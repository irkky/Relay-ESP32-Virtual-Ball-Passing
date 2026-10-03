#pragma once
#include <stdint.h>
#include "config.h"
// Portable edge detector: acquisition and release both require stable input.
class JoystickEdge {
  bool armed_=false, wasActive_=false;
  int candidate_=0;
  uint32_t since_=0;
public:
  int update(int x, int y, bool active, uint32_t now) {
    if(!active || !wasActive_) { armed_=false; candidate_=0; since_=now; }
    wasActive_=active;
    if(!active) return 0;
    int dx=x-cfg::ADC_CENTER, dy=y-cfg::ADC_CENTER;
    if(cfg::INVERT_X) dx=-dx;
    bool centered=dx>=-cfg::CENTER_ZONE && dx<=cfg::CENTER_ZONE && dy>=-cfg::CENTER_ZONE && dy<=cfg::CENTER_ZONE;
    int intent=centered ? 2 : (dy>-cfg::DEADZONE && dy<cfg::DEADZONE ? (dx < -cfg::DEADZONE ? -1 : dx > cfg::DEADZONE ? 1 : 0) : 0);
    if(intent!=candidate_) { candidate_=intent; since_=now; }
    if(uint32_t(now-since_)<cfg::DEBOUNCE_MS) return 0;
    if(intent==2) armed_=true;
    if(armed_ && (intent==-1 || intent==1)) { armed_=false; return intent; }
    return 0;
  }
};
