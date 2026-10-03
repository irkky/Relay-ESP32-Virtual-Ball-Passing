#include "protocol.h"
namespace ball {
static void put32(uint8_t* b, uint32_t n) { for (int i=0;i<4;i++) b[i] = uint8_t(n >> (8*i)); }
static uint32_t get32(const uint8_t* b) { uint32_t n=0; for (int i=0;i<4;i++) n |= uint32_t(b[i]) << (8*i); return n; }
static uint32_t crc(const uint8_t* data, size_t len) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i=0;i<len;i++) { c ^= data[i]; for (int j=0;j<8;j++) c = (c >> 1) ^ (0xEDB88320u & (0u-(c & 1))); }
  return ~c;
}
void encode(const Packet& p, uint8_t* b) {
  b[0]='V'; b[1]='B'; b[2]=1; b[3]=uint8_t(p.type);
  b[4]=p.from; b[5]=p.to; b[6]=p.origin; b[7]=p.target;
  b[8]=uint8_t(p.direction); b[9]=b[10]=b[11]=0;
  put32(b+12,p.epoch); put32(b+16,p.seq); put32(b+20,p.fromBoot);
  put32(b+24,p.toBoot); put32(b+28,p.value); put32(b+32,crc(b,32));
}
bool decode(const uint8_t* b, size_t n, uint8_t radioFrom, uint8_t self, Packet& p) {
  if(n != WIRE_SIZE || b[0]!='V' || b[1]!='B' || b[2]!=1 || b[3]<1 || b[3]>15 ||
     !findPlayerByID(radioFrom) || b[4]!=radioFrom || b[5]!=self || b[6]>COUNT || b[7]>COUNT ||
     (b[8]!=0 && b[8]!=1 && b[8]!=255) || b[9] || b[10] || b[11] || get32(b+32)!=crc(b,32)) return false;
  p.type=Type(b[3]); p.from=b[4]; p.to=b[5]; p.origin=b[6]; p.target=b[7]; p.direction=int8_t(b[8]);
  p.epoch=get32(b+12); p.seq=get32(b+16); p.fromBoot=get32(b+20); p.toBoot=get32(b+24); p.value=get32(b+28);
  return p.fromBoot != 0;
}
const char* typeName(Type t) {
  static const char* names[]={"INVALID","HELLO","RESET","RESET_ACK","BALL_START","BALL_PASS","BALL_RECEIVED","READY","RELEASE","GRANT","GRANT_ACK","SYNC","HEARTBEAT","RETRY","FAULT","DIRECTORY"};
  auto i=uint8_t(t); return i<=15 ? names[i] : names[0];
}
}
