#pragma once
#include "config.h"
#include "protocol.h"

namespace ball {
enum class State : uint8_t { WAITING, RESETTING, IDLE, ACTIVE, PASSING, STAGED, GRANTING, ERROR };
const char* stateName(State state);
struct Port {
  virtual ~Port() = default;
  virtual void send(const Packet&) = 0;
  virtual void event(const char* type, const char* text, uint8_t from, uint8_t to) = 0;
  virtual bool saveEpoch(uint32_t) = 0;
};
class Game {
public:
  Game(uint8_t id, uint32_t boot, uint32_t storedEpoch, Port& port);
  void begin(uint32_t now);
  void receive(const Packet& p, uint32_t now);
  void tick(uint32_t now);
  bool start(uint8_t target, uint32_t now);
  bool reset(uint32_t now);
  void retry(uint32_t now);
  bool move(int8_t direction, uint32_t now);
  bool online(uint8_t id, uint32_t now) const;
  uint8_t id, holder=0, previous=0, target=0;
  uint32_t boot, epoch, seq=0, errors=0, lastPacket=0, lastAck=0;
  State state=State::WAITING;
  bool active=false, paused=false, synced=false, fatal=false;
  uint32_t peerBoot[COUNT+1]={}, lastSeen[COUNT+1]={};
  bool seen[COUNT+1]={}, resetAck[COUNT+1]={};
  State reported[COUNT+1]={};
private:
  Port& io;
  uint32_t now_=0, lastTry_=0, lastHeartbeat_=0, transactionSeq_=0, fence_=0, surrendered_=0;
  uint8_t attempts_=0, origin_=0;
  int8_t direction_=0;
  bool received_=false, released_=false, ready_=false;
  void emit(Type type, uint8_t to, uint8_t origin=0, uint8_t target=0, uint32_t sequence=0, int8_t direction=0, uint32_t value=0);
  void transmit();
  void beginRetry();
  void fail(const char* text);
  void sync(uint8_t to);
  bool validRoute(const Packet& p) const;
  void masterReceive(const Packet& p);
  void slaveReceive(const Packet& p);
};
}
