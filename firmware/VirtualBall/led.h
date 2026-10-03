#pragma once
#include <Arduino.h>
#include "config.h"
inline void setLEDColor(bool red, bool green, bool blue) {
  digitalWrite(cfg::RED_PIN, red != cfg::COMMON_ANODE);
  digitalWrite(cfg::GREEN_PIN, green != cfg::COMMON_ANODE);
  digitalWrite(cfg::BLUE_PIN, blue != cfg::COMMON_ANODE);
}
inline void beginLED() {
  pinMode(cfg::RED_PIN,OUTPUT); pinMode(cfg::GREEN_PIN,OUTPUT); pinMode(cfg::BLUE_PIN,OUTPUT);
  setLEDColor(false,false,false);
}
