#pragma once
#include <Arduino.h>
#include "protocol.h"
namespace radio {
bool begin(uint8_t self);
bool sendPacket(const ball::Packet& p);
bool receive(ball::Packet& p);
void pump();
uint32_t failures();
uint32_t rejected();
uint32_t dropped();
}
