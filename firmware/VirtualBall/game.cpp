#include "game.h"
#include <limits.h>
#include <initializer_list>
namespace ball {
const char* stateName(State s) {
  static const char* names[]={"WAITING","RESETTING","IDLE","ACTIVE","PASSING","STAGED","GRANTING","ERROR"};
  return uint8_t(s)<8 ? names[uint8_t(s)] : "UNKNOWN";
}
Game::Game(uint8_t i, uint32_t b, uint32_t e, Port& p): id(i), boot(b), epoch(e), io(p) {
  peerBoot[id]=boot;
}
void Game::emit(Type t, uint8_t to, uint8_t origin, uint8_t dest, uint32_t s, int8_t d, uint32_t v) {
  if(!findPlayerByID(to) || to==id) return;
  Packet p; p.type=t; p.from=id; p.to=to; p.origin=origin; p.target=dest; p.epoch=epoch;
  p.seq=s; p.direction=d; p.fromBoot=boot; p.toBoot=peerBoot[to]; p.value=v; io.send(p);
}
void Game::fail(const char* text) { ++errors; io.event("ERROR",text,id,target); }
void Game::begin(uint32_t now) {
  now_=now; lastHeartbeat_=now;
  if(id==MASTER) reset(now); else emit(Type::HELLO,MASTER,0,0,0,0,boot);
}
bool Game::online(uint8_t i, uint32_t now) const {
  return i==id || (i<=COUNT && seen[i] && uint32_t(now-lastSeen[i])<cfg::OFFLINE_MS);
}
void Game::beginRetry() { paused=false; attempts_=0; lastTry_=now_-cfg::ACK_TIMEOUT_MS; }
bool Game::reset(uint32_t now) {
  if(id!=MASTER || fatal) return false;
  now_=now;
  if(epoch==UINT32_MAX || !io.saveEpoch(epoch+1)) { fatal=true; state=State::ERROR; fail("Epoch storage failed; halt and repair NVS"); return false; }
  ++epoch; seq=0; holder=previous=target=origin_=0; active=false; state=State::RESETTING;
  released_=ready_=false;
  for(uint8_t i=2;i<=COUNT;i++) resetAck[i]=false;
  beginRetry(); io.event("GAME_RESET","Reset barrier: waiting for all seven slaves",MASTER,0); return true;
}
bool Game::start(uint8_t dest, uint32_t now) {
  now_=now;
  if(id!=MASTER || fatal || state!=State::IDLE || dest<=MASTER || dest>COUNT || !online(dest,now)) { fail("Start requires completed reset and an online slave"); return false; }
  for(uint8_t i=2;i<=COUNT;i++) if(!resetAck[i] || !online(i,now)) { fail("All seven slaves must be online to start"); return false; }
  seq=1; holder=target=dest; previous=MASTER; state=State::GRANTING; beginRetry();
  io.event("GAME_STARTED","Delivering initial ball",MASTER,dest); return true;
}
void Game::sync(uint8_t to) {
  emit(Type::SYNC,to,previous,holder,seq);
  for(uint8_t n : {getLeftNeighbor(to),getRightNeighbor(to)})
    if(n && peerBoot[n]) emit(Type::DIRECTORY,to,n,0,0,0,peerBoot[n]);
}
void Game::retry(uint32_t now) {
  if(fatal) return;
  now_=now; beginRetry();
  if(id==MASTER) for(uint8_t i=2;i<=COUNT;i++) emit(Type::RETRY,i,previous,holder,seq);
  io.event("RETRY","Retrying the same transaction",id,target);
}
bool Game::move(int8_t dir, uint32_t now) {
  now_=now;
  if(id==MASTER || !active || state!=State::ACTIVE || fatal) return false;
  auto dest = dir==-1 ? getLeftNeighbor(id) : dir==1 ? getRightNeighbor(id) : 0;
  if(!dest) { fail("No neighbor in this direction"); emit(Type::FAULT,MASTER,id,0,seq,dir,1); return false; }
  if(!peerBoot[dest]) { fail("Neighbor boot identity not yet discovered; center and retry"); return false; }
  if(seq==UINT32_MAX) { fail("Sequence exhausted; reset required"); return false; }
  // Irrevocable local surrender BEFORE any packet can reach the next player.
  active=false; surrendered_=seq; transactionSeq_=seq+1; fence_=transactionSeq_;
  origin_=id; target=dest; direction_=dir; received_=false; state=State::PASSING;
  beginRetry(); io.event("BALL_PASS","Passing to neighbor",id,dest); return true;
}
bool Game::validRoute(const Packet& p) const {
  return adjacent(p.origin,p.target) && ((p.direction==-1 && getLeftNeighbor(p.origin)==p.target) ||
      (p.direction==1 && getRightNeighbor(p.origin)==p.target));
}
void Game::receive(const Packet& p, uint32_t now) {
  now_=now;
  if(fatal || p.to!=id || !findPlayerByID(p.from) || p.from==id || !p.fromBoot) return;
  // HELLO is the only discovery packet without a destination incarnation.
  if(p.type!=Type::HELLO && p.toBoot!=boot) return;
  if(id==MASTER && p.type==Type::HELLO && p.from>MASTER && p.value==p.fromBoot) {
    if(peerBoot[p.from] && p.fromBoot<peerBoot[p.from]) return;
    bool changed=peerBoot[p.from]!=p.fromBoot;
    peerBoot[p.from]=p.fromBoot; seen[p.from]=true; lastSeen[p.from]=now; lastPacket=now;
    if(changed && state!=State::RESETTING) reset(now);
    if(state==State::RESETTING) { if(changed) { resetAck[p.from]=false; beginRetry(); } emit(Type::RESET,p.from); }
    else sync(p.from);
    return;
  }
  if(id!=MASTER && p.from==MASTER && p.type==Type::RESET) {
    if(p.epoch<epoch || !p.epoch) return;
    if(p.epoch>epoch && !io.saveEpoch(p.epoch)) { active=false; fatal=true; state=State::ERROR; fail("Cannot persist reset epoch"); return; }
    // Duplicate resets from the CURRENT epoch only acknowledge: they cannot erase a live ball.
    if(p.epoch>epoch || !synced) {
      epoch=p.epoch; seq=fence_=surrendered_=transactionSeq_=0; holder=previous=target=origin_=0;
      active=paused=received_=false; state=State::WAITING; synced=true; peerBoot[MASTER]=p.fromBoot;
      io.event("GAME_RESET","Waiting for start",MASTER,id);
    }
    if(peerBoot[MASTER]!=p.fromBoot) return;
    seen[MASTER]=true; lastSeen[MASTER]=now; lastPacket=now;
    emit(Type::RESET_ACK,MASTER); return;
  }
  if(p.epoch!=epoch || !epoch) return;
  if(id==MASTER) { if(peerBoot[p.from]!=p.fromBoot) return; }
  else if(!synced || (p.from==MASTER && p.fromBoot!=peerBoot[MASTER])) return;
  // Neighbor boot values are learned only from addressed, valid-session frames.
  if(p.from!=MASTER && id!=MASTER) {
    if(!adjacent(id,p.from) || (peerBoot[p.from] && p.fromBoot<peerBoot[p.from])) return;
    peerBoot[p.from]=p.fromBoot;
  }
  seen[p.from]=true; lastSeen[p.from]=now; lastPacket=now;
  if(id==MASTER) masterReceive(p); else slaveReceive(p);
}
void Game::masterReceive(const Packet& p) {
  if(p.type==Type::RESET_ACK && state==State::RESETTING) {
    resetAck[p.from]=true; lastAck=now_;
    bool all=true; for(uint8_t i=2;i<=COUNT;i++) all &= resetAck[i];
    if(all) { state=State::IDLE; paused=false; for(uint8_t i=2;i<=COUNT;i++) sync(i); io.event("RESET_COMPLETE","All players cleared; ready to start",MASTER,0); }
    return;
  }
  if(p.type==Type::HEARTBEAT) { if(p.value<=uint8_t(State::ERROR)) reported[p.from]=State(p.value); if(state!=State::RESETTING) sync(p.from); return; }
  if(p.type==Type::FAULT) { fail(p.value==1 ? "Player has no neighbor in that direction" : "Player input fault"); return; }
  if(p.type==Type::GRANT_ACK && state==State::GRANTING && p.from==holder && p.seq==seq && p.target==holder && p.origin==previous) {
    lastAck=now_; state=State::ACTIVE; paused=false;
    io.event("BALL_RECEIVED","Ball activation acknowledged",previous,holder);
    for(uint8_t i=2;i<=COUNT;i++) sync(i);
    return;
  }
  if(p.type!=Type::READY && p.type!=Type::RELEASE) return;
  if(!validRoute(p) || (p.type==Type::READY && p.from!=p.target) || (p.type==Type::RELEASE && p.from!=p.origin)) { fail("Invalid transfer route or sender"); return; }
  if(p.seq<=seq) {
    sync(p.from);
    if(p.seq==seq && p.target==holder && p.origin==previous && state==State::GRANTING) transmit();
    return;
  }
  if((state!=State::ACTIVE && state!=State::PASSING && state!=State::GRANTING) || p.seq!=seq+1 || p.origin!=holder) { fail("Rejected stale or unauthorized transfer"); return; }
  // A READY/RELEASE for the next sequence also proves that a lost GRANT_ACK did not prevent delivery.
  if(state!=State::PASSING) {
    state=State::PASSING; origin_=p.origin; target=p.target; direction_=p.direction;
    transactionSeq_=p.seq; released_=ready_=false; beginRetry();
    io.event("BALL_PASS","Transfer awaiting receiver and sender confirmation",origin_,target);
  }
  if(target!=p.target || origin_!=p.origin || direction_!=p.direction) { fail("Conflicting transfer"); return; }
  if(p.type==Type::READY) ready_=true; else released_=true;
  lastAck=now_;
  if(ready_ && released_) {
    previous=holder; holder=target; seq=transactionSeq_; state=State::GRANTING; beginRetry();
    sync(previous); // retires the sender's pending transaction; never activates anyone
  }
}
void Game::slaveReceive(const Packet& p) {
  if(p.from==MASTER && p.type==Type::DIRECTORY && adjacent(id,p.origin) && p.value) { peerBoot[p.origin]=p.value; return; }
  if(p.from==MASTER && p.type==Type::RETRY) { beginRetry(); return; }
  if(p.from==MASTER && p.type==Type::SYNC) {
    if(p.seq<fence_) return;
    fence_=p.seq; holder=p.target; previous=p.origin;
    if(p.target!=id) {
      active=false;
      if((state==State::PASSING || state==State::STAGED) && p.seq>=transactionSeq_) { state=State::WAITING; paused=false; target=0; }
      if(state==State::ACTIVE) state=State::WAITING;
    }
    return;
  }
  if(p.from==MASTER && (p.type==Type::BALL_START || p.type==Type::GRANT)) {
    bool start=p.type==Type::BALL_START;
    if(p.target!=id || !p.seq || p.seq<fence_ || p.seq<=surrendered_ ||
       (start && (p.seq!=1 || p.origin!=MASTER)) ||
       (!start && (!adjacent(p.origin,id) || (state!=State::ACTIVE && !(state==State::STAGED && transactionSeq_==p.seq && origin_==p.origin))))) return;
    // Same grant cannot rearm the joystick or resurrect a surrendered sequence.
    bool fresh=!active;
    active=true; paused=false; state=State::ACTIVE; holder=id; previous=p.origin; seq=fence_=p.seq; target=0;
    emit(Type::GRANT_ACK,MASTER,p.origin,id,p.seq);
    if(fresh) io.event("BALL_RECEIVED","You have the ball",p.origin,id);
    return;
  }
  if(p.type==Type::BALL_PASS && p.from==p.origin && p.target==id && validRoute(p)) {
    if(active || p.seq<fence_ || p.seq<=surrendered_ || !p.seq || state==State::PASSING) return;
    if(state==State::STAGED && (p.seq!=transactionSeq_ || p.origin!=origin_)) return;
    bool fresh=state!=State::STAGED;
    state=State::STAGED; transactionSeq_=p.seq; fence_=p.seq; origin_=p.origin; target=id; direction_=p.direction;
    if(fresh) beginRetry();
    emit(Type::BALL_RECEIVED,p.from,p.origin,id,p.seq,p.direction);
    emit(Type::READY,MASTER,p.origin,id,p.seq,p.direction); return;
  }
  if(p.type==Type::BALL_RECEIVED && state==State::PASSING && p.from==target && p.seq==transactionSeq_ && p.origin==id && p.target==target && p.direction==direction_) {
    received_=true; lastAck=now_; beginRetry(); emit(Type::RELEASE,MASTER,id,target,transactionSeq_,direction_);
  }
}
void Game::transmit() {
  if(id==MASTER) {
    if(state==State::RESETTING) for(uint8_t i=2;i<=COUNT;i++) { if(peerBoot[i] && !resetAck[i]) emit(Type::RESET,i); }
    if(state==State::GRANTING) emit(seq==1 ? Type::BALL_START : Type::GRANT,holder,previous,holder,seq);
    if(state==State::PASSING) { emit(Type::RETRY,origin_); emit(Type::RETRY,target); }
  } else {
    if(state==State::PASSING) emit(received_ ? Type::RELEASE : Type::BALL_PASS,received_ ? MASTER : target,id,target,transactionSeq_,direction_);
    if(state==State::STAGED) emit(Type::READY,MASTER,origin_,id,transactionSeq_,direction_);
  }
}
void Game::tick(uint32_t now) {
  now_=now; if(fatal) return;
  if(uint32_t(now-lastHeartbeat_)>=cfg::HEARTBEAT_MS + (id==MASTER ? 0 : id*17)) {
    lastHeartbeat_=now;
    if(id!=MASTER) {
      emit(Type::HELLO,MASTER,0,0,0,0,boot);
      if(synced) emit(Type::HEARTBEAT,MASTER,0,0,seq,0,uint8_t(state));
    }
  }
  bool pending=state==State::RESETTING || state==State::PASSING || state==State::STAGED || state==State::GRANTING;
  if(pending && !paused && uint32_t(now-lastTry_)>=cfg::ACK_TIMEOUT_MS) {
    if(attempts_>=cfg::RETRY_COUNT+1) { paused=true; fail("ACK timeout; ownership retained as uncertain. Retry or reset"); }
    else { ++attempts_; lastTry_=now; transmit(); }
  }
}
}
