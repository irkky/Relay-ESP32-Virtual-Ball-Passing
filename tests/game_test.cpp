#include "game.h"
#include "joystick.h"
#include <algorithm>
#include <cassert>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <random>
#include <vector>
using namespace ball;
struct Network;
struct TestPort : Port {
  Network* net=nullptr; uint8_t id=0; uint32_t saved=0; bool storageOK=true;
  void send(const Packet&) override;
  void event(const char*,const char*,uint8_t,uint8_t) override {}
  bool saveEpoch(uint32_t epoch) override { if(!storageOK) return false; saved=epoch; return true; }
};
struct Network {
  TestPort ports[9]; std::unique_ptr<Game> nodes[9]; bool connected[9]={};
  std::deque<Packet> queue; std::vector<Packet> history;
  uint32_t now=1; std::function<bool(const Packet&)> drop=[](const Packet&){return false;};
  bool shuffle=false, duplicate=false; unsigned lossPercent=0; std::mt19937 rng{12345};
  Network() {
    for(uint8_t i=1;i<=8;i++) { ports[i].net=this; ports[i].id=i; connected[i]=true; nodes[i].reset(new Game(i,1,0,ports[i])); }
    for(uint8_t i=1;i<=8;i++) nodes[i]->begin(now);
    run(3000); assert(nodes[1]->state==State::IDLE);
  }
  void invariant() {
    unsigned count=0; for(int i=2;i<=8;i++) count+=nodes[i]->active;
    if(count>1) { std::cerr<<"Multiple balls at "<<now<<"\n"; std::abort(); }
  }
  void pump() {
    unsigned budget=1000;
    while(!queue.empty() && budget--) {
      size_t at=shuffle ? rng()%queue.size() : 0;
      auto p=queue[at]; queue.erase(queue.begin()+at);
      if(!connected[p.to] || !connected[p.from] || drop(p) || (lossPercent && rng()%100<lossPercent)) continue;
      nodes[p.to]->receive(p,now); invariant();
      if(duplicate) { nodes[p.to]->receive(p,now); invariant(); }
    }
    assert(budget>0);
  }
  void run(uint32_t duration) {
    for(uint32_t t=0;t<duration;t+=20) { now+=20; for(int i=1;i<=8;i++) if(connected[i]) nodes[i]->tick(now); pump(); invariant(); }
  }
  void start(uint8_t who) { assert(nodes[1]->start(who,now)); run(800); assert(nodes[who]->active); assert(nodes[1]->holder==who); }
  void reset() { assert(nodes[1]->reset(now)); run(800); assert(nodes[1]->state==State::IDLE); }
  void pass(uint8_t from, int dir, uint8_t to) { assert(nodes[from]->move(dir,now)); invariant(); run(1200); assert(nodes[to]->active); assert(nodes[1]->holder==to); }
  void reboot(uint8_t i) { auto boot=nodes[i]->boot+1; nodes[i].reset(new Game(i,boot,ports[i].saved,ports[i])); nodes[i]->begin(now); }
};
void TestPort::send(const Packet& p) { net->queue.push_back(p); net->history.push_back(p); }

void routes() {
  Network n;
  for(uint8_t i=2;i<=8;i++) { n.start(i); n.reset(); }
  n.start(2); n.pass(2,1,3); n.pass(3,-1,2); n.pass(2,1,3); n.pass(3,1,4);
  n.pass(4,1,5); n.pass(5,-1,4); n.reset(); n.start(8); n.pass(8,-1,7);
  n.reset(); n.start(4); n.pass(4,-1,3); n.pass(3,1,4); n.pass(4,-1,3); n.pass(3,-1,2);
  assert(!n.nodes[2]->move(-1,n.now)); assert(n.nodes[2]->active);
  assert(!n.nodes[6]->move(1,n.now)); n.pass(2,1,3);
  n.reset(); n.start(8); assert(!n.nodes[8]->move(1,n.now));
  n.connected[5]=false; n.run(cfg::OFFLINE_MS+2500); assert(!n.nodes[1]->online(5,n.now));
  assert(n.nodes[1]->reset(n.now)); n.run(5000); assert(n.nodes[1]->state==State::RESETTING); assert(!n.nodes[1]->start(2,n.now));
  n.connected[5]=true; n.run(5000); assert(n.nodes[1]->state==State::IDLE);
}
void failures() {
  for(Type type : {Type::BALL_PASS,Type::BALL_RECEIVED,Type::READY,Type::RELEASE,Type::GRANT,Type::GRANT_ACK}) {
    Network n; n.start(3);
    n.drop=[type](const Packet& p){return p.type==type;};
    assert(n.nodes[3]->move(1,n.now)); n.run(7000);
    assert(!n.nodes[3]->active);
    assert(n.nodes[1]->errors+n.nodes[3]->errors+n.nodes[4]->errors>0);
    n.drop=[](const Packet&){return false;}; n.nodes[1]->retry(n.now); n.run(5000);
    assert(n.nodes[4]->active); assert(n.nodes[1]->holder==4);
    n.duplicate=true; n.pass(4,-1,3);
    // Replay every old packet including the first start grant and reset.
    auto old=n.history; for(const auto& p:old) { n.nodes[p.to]->receive(p,n.now); n.invariant(); }
    n.run(500); assert(n.nodes[3]->active);
  }
  Network n; n.drop=[](const Packet&p){return p.type==Type::GRANT_ACK;};
  assert(n.nodes[1]->start(4,n.now)); n.run(900); assert(n.nodes[4]->active);
  // A user may act before an activation ACK reaches the Master.
  assert(n.nodes[4]->move(-1,n.now)); n.run(1800); assert(n.nodes[3]->active); assert(n.nodes[1]->holder==3);
}
void restarts() {
  for(uint8_t rebootID : {1,3,4,7}) {
    Network n; n.start(3); auto stale=n.history; n.connected[3]=false;
    n.reboot(rebootID); n.run(5000);
    assert(!n.nodes[1]->start(4,n.now));
    n.connected[3]=true; n.run(6000); assert(n.nodes[1]->state==State::IDLE);
    n.start(4);
    for(const auto& p:stale) { n.nodes[p.to]->receive(p,n.now); n.invariant(); }
    assert(n.nodes[4]->active);
  }
  Network n; n.start(4); n.nodes[4]->move(1,n.now); n.run(40); n.reboot(5); n.run(6000);
  assert(n.nodes[1]->state==State::IDLE);
  n.ports[1].storageOK=false; assert(!n.nodes[1]->reset(n.now)); assert(n.nodes[1]->fatal);
}
void validation() {
  Network n; n.start(3);
  Packet bad; bad.type=Type::BALL_PASS; bad.from=3; bad.to=8; bad.origin=3; bad.target=8; bad.direction=1;
  bad.epoch=n.nodes[1]->epoch; bad.seq=2; bad.fromBoot=1; bad.toBoot=1;
  n.nodes[8]->receive(bad,n.now); assert(!n.nodes[8]->active); assert(n.nodes[8]->state==State::WAITING);
  bad.type=Type::RELEASE; bad.to=1; n.nodes[1]->receive(bad,n.now); assert(n.nodes[1]->holder==3);
  uint8_t bytes[WIRE_SIZE]; encode(bad,bytes); Packet decoded;
  assert(decode(bytes,WIRE_SIZE,3,1,decoded)); assert(decoded.seq==2);
  assert(!decode(bytes,WIRE_SIZE,4,1,decoded)); assert(!decode(bytes,WIRE_SIZE,3,8,decoded)); assert(!decode(bytes,WIRE_SIZE-1,3,1,decoded));
  bytes[16]^=0x01; assert(!decode(bytes,WIRE_SIZE,3,1,decoded));
  bad.type=Type::BALL_START; bad.from=1; bad.to=8; bad.origin=1; bad.seq=1; bad.toBoot=99;
  n.nodes[8]->receive(bad,n.now); assert(!n.nodes[8]->active);
}
void joystick() {
  JoystickEdge j;
  assert(j.update(0,2048,true,1)==0); assert(j.update(0,2048,true,100)==0);
  j.update(2048,2048,true,110); assert(j.update(2048,2048,true,200)==0);
  j.update(0,2048,true,220); assert(j.update(0,2048,true,300)==-1);
  for(int i=0;i<100;i++) assert(j.update(0,2048,true,400+i*20)==0);
  j.update(2048,2048,false,2500); j.update(4095,2048,true,2600); assert(j.update(4095,2048,true,2800)==0);
  j.update(2048,2048,true,2900); j.update(2048,2048,true,3000);
  j.update(2048,4095,true,3100); assert(j.update(2048,4095,true,3300)==0);
  j.update(4095,2048,true,3400); assert(j.update(4095,2048,true,3500)==1);
}
void randomized() {
  for(unsigned seed=1;seed<=40;seed++) {
    Network n; n.rng.seed(seed); n.shuffle=n.duplicate=true; n.lossPercent=22;
    n.nodes[1]->start(4,n.now);
    for(int step=0;step<300;step++) {
      n.run(100);
      if(step%17==0) n.nodes[1]->retry(n.now);
      for(uint8_t i=2;i<=8;i++) if(n.nodes[i]->active && n.rng()%4==0) { n.nodes[i]->move(n.rng()%2 ? -1 : 1,n.now); break; }
      if(step%31==0 && !n.history.empty()) { const auto p=n.history[n.rng()%n.history.size()]; n.nodes[p.to]->receive(p,n.now); n.invariant(); }
      if(step==140) n.nodes[1]->reset(n.now);
      if(n.nodes[1]->state==State::IDLE) n.nodes[1]->start(2+n.rng()%7,n.now);
    }
  }
}
int main() {
  routes(); std::cout<<"PASS: all requested routes, end rejection, inactive input, offline, reset barrier\n";
  failures(); std::cout<<"PASS: packet/ACK loss, bounded retry, recovery, duplicate and delayed grants\n";
  restarts(); std::cout<<"PASS: Master/slave reboot, partition, in-flight restart, storage failure\n";
  validation(); joystick(); std::cout<<"PASS: packet validation, boot fencing, joystick center/debounce/one-shot\n";
  randomized(); std::cout<<"PASS: 40 seeded fault runs (loss, duplicates, reorder, replay, resets); single-ball invariant\n";
}
