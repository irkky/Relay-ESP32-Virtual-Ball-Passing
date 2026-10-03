#pragma once
#include <stddef.h>
#include <stdint.h>
#include "players.h"

namespace ball {
enum class Type : uint8_t {
  HELLO = 1, RESET, RESET_ACK, BALL_START, BALL_PASS, BALL_RECEIVED,
  READY, RELEASE, GRANT, GRANT_ACK, SYNC, HEARTBEAT, RETRY, FAULT, DIRECTORY
};
// MACs are bound to from/to through players[] and verified against radio metadata.
struct Packet {
  Type type = Type::HELLO;
  uint8_t from = 0, to = 0, origin = 0, target = 0;
  int8_t direction = 0;
  uint32_t epoch = 0, seq = 0, fromBoot = 0, toBoot = 0;
  uint32_t value = 0; // HELLO: boot; heartbeat: local state; FAULT: code
};
constexpr size_t WIRE_SIZE = 36;
void encode(const Packet& p, uint8_t* out);
bool decode(const uint8_t* data, size_t len, uint8_t radioFrom, uint8_t self, Packet& out);
const char* typeName(Type type);
}
