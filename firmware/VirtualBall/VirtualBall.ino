// The SAME sketch is flashed to all eight boards. Role comes from STA MAC.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <Preferences.h>
#include "game.h"
#include "radio.h"
#include "joystick.h"
#include "led.h"

using namespace ball;
Preferences storage;
Game* game=nullptr;
bool radioOK=false;
uint32_t inputErrorUntil=0;

template <typename T> void jsonLine(const T& doc) { serializeJson(doc,Serial); Serial.write('\n'); }
void eventLine(const char* type, const char* message, uint8_t from=0, uint8_t to=0) {
  StaticJsonDocument<384> d;
  d["type"]=type; d["message"]=message; d["from"]=from; d["to"]=to; d["uptimeMs"]=millis();
  if(game) { d["gameId"]=game->seq ? game->epoch : 0; d["sequence"]=game->seq; }
  jsonLine(d);
}
struct HardwarePort : Port {
  void send(const Packet& p) override { radio::sendPacket(p); }
  void event(const char* type, const char* text, uint8_t from, uint8_t to) override {
    eventLine(type,text,from,to);
    if(strcmp(type,"ERROR")==0) inputErrorUntil=millis()+1200;
  }
  bool saveEpoch(uint32_t epoch) override { return storage.putUInt("epoch",epoch)==sizeof(uint32_t); }
} hardware;

void configLine() {
  StaticJsonDocument<2304> d; d["type"]="CONFIG"; d["protocol"]=1;
  d["deviceId"]=game->id; d["role"]=game->id==MASTER ? "MASTER" : "SLAVE";
  d["baudRate"]=cfg::SERIAL_BAUD; d["channel"]=cfg::WIFI_CHANNEL;
  JsonArray list=d.createNestedArray("players");
  for(const auto& p: players) {
    auto item=list.createNestedObject(); item["id"]=p.id; item["name"]=p.name;
    item["role"]=p.id==MASTER ? "MASTER" : "SLAVE"; item["left"]=p.left; item["right"]=p.right;
    char mac[18]; snprintf(mac,sizeof(mac),"%02X:%02X:%02X:%02X:%02X:%02X",p.mac[0],p.mac[1],p.mac[2],p.mac[3],p.mac[4],p.mac[5]); item["mac"]=mac;
  }
  jsonLine(d);
}
void statusLine() {
  if(!game || game->id!=MASTER) return;
  const uint32_t now=millis();
  StaticJsonDocument<2304> d; d["type"]="GAME_STATE"; d["uptimeMs"]=now;
  d["epoch"]=game->epoch; d["gameId"]=(game->state==State::IDLE || game->state==State::RESETTING) ? 0 : game->epoch;
  d["sequence"]=game->seq; d["state"]=stateName(game->state); d["currentPlayer"]=game->holder;
  d["previousPlayer"]=game->previous; d["targetPlayer"]=game->target; d["paused"]=game->paused;
  d["espnow"]=radioOK; d["errors"]=game->errors; d["sendFailures"]=radio::failures();
  d["invalidPackets"]=radio::rejected(); d["queueDrops"]=radio::dropped();
  d["lastPacketMs"]=game->lastPacket; d["lastAckMs"]=game->lastAck;
  auto list=d.createNestedArray("players");
  for(uint8_t i=1;i<=COUNT;i++) {
    auto p=list.createNestedObject(); p["id"]=i;
    p["online"]=i==MASTER ? "ONLINE" : !game->seen[i] ? "UNKNOWN" : game->online(i,now) ? "ONLINE" : "OFFLINE";
    p["resetAck"]=game->resetAck[i]; p["state"]=i==MASTER ? stateName(game->state) : stateName(game->reported[i]);
    p["lastSeenMs"]=game->lastSeen[i];
  }
  jsonLine(d);
}
void commandLine(const char* line) {
  StaticJsonDocument<512> d;
  if(deserializeJson(d,line) || !d.is<JsonObject>() || !d["type"].is<const char*>()) { eventLine("ERROR","Invalid JSON command"); return; }
  const char* type=d["type"];
  if(strcmp(type,"GET_CONFIG")==0) { configLine(); return; }
  if(game->id!=MASTER) { eventLine("ERROR","Connect USB to Ramya's Master board"); return; }
  if(strcmp(type,"GET_STATUS")==0) statusLine();
  else if(strcmp(type,"START_GAME")==0) {
    if(!d["player"].is<int>() || d["player"].as<int>()<2 || d["player"].as<int>()>COUNT) eventLine("ERROR","player must be an integer from 2 to 8");
    else game->start(d["player"].as<uint8_t>(),millis());
    statusLine();
  } else if(strcmp(type,"RESET_GAME")==0) { game->reset(millis()); statusLine(); }
  else if(strcmp(type,"RETRY")==0) { game->retry(millis()); statusLine(); }
  else eventLine("ERROR","Unknown command");
}
void readSerial() {
  static char line[cfg::SERIAL_MAX_LINE+1]; static size_t used=0; static bool discard=false;
  // Bounded work per loop preserves radio servicing under serial noise.
  for(int n=0;n<128 && Serial.available();n++) {
    char c=char(Serial.read());
    if(c=='\n') {
      if(discard) eventLine("ERROR","Serial line too long; discarded");
      else if(used) { line[used]=0; commandLine(line); }
      used=0; discard=false;
    } else if(c!='\r' && !discard) { if(used<cfg::SERIAL_MAX_LINE) line[used++]=c; else discard=true; }
  }
}
void setup() {
  Serial.begin(cfg::SERIAL_BAUD); beginLED();
  WiFi.mode(WIFI_STA); WiFi.disconnect(); WiFi.setSleep(false);
  uint8_t mac[6]; WiFi.macAddress(mac); const uint8_t id=findPlayerByMAC(mac);
  StaticJsonDocument<384> identity; identity["type"]="IDENTITY"; identity["mac"]=WiFi.macAddress();
  identity["id"]=id; identity["role"]=id==MASTER ? "MASTER" : id ? "SLAVE" : "UNKNOWN";
  if(id) { identity["name"]=findPlayerByID(id)->name; identity["left"]=getLeftNeighbor(id); identity["right"]=getRightNeighbor(id); }
  jsonLine(identity);
  if(!id) { eventLine("ERROR","UNKNOWN MAC: participation disabled. Update central config if this is a replacement board."); setLEDColor(true,false,false); return; }
  if(!storage.begin("virtualball",false)) { eventLine("ERROR","Cannot open NVS"); setLEDColor(true,false,false); return; }
  uint32_t boot=storage.getUInt("boot",0);
  if(boot==UINT32_MAX || storage.putUInt("boot",boot+1)!=sizeof(uint32_t)) { eventLine("ERROR","Cannot persist boot counter"); setLEDColor(true,false,false); return; }
  radioOK=radio::begin(id);
  if(!radioOK) { eventLine("ERROR","ESP-NOW initialization/channel/peer registration failed"); setLEDColor(true,false,false); return; }
  static Game instance(id,boot+1,storage.getUInt("epoch",0),hardware); game=&instance;
  if(id!=MASTER) { pinMode(cfg::JOY_X,INPUT); pinMode(cfg::JOY_Y,INPUT); pinMode(cfg::JOY_SW,INPUT_PULLUP); analogReadResolution(12); analogSetPinAttenuation(cfg::JOY_X,ADC_11db); analogSetPinAttenuation(cfg::JOY_Y,ADC_11db); }
  game->begin(millis()); configLine(); statusLine();
}
void loop() {
  if(!game) { delay(20); return; }
  const uint32_t now=millis();
  Packet p; for(int n=0;n<24 && radio::receive(p);n++) game->receive(p,now);
  game->tick(now); readSerial();
  static uint32_t sampled=0, lastStatus=0, railSince=0; static bool railWarned=false;
  static uint32_t joystickEpoch=0, joystickSequence=0;
  static JoystickEdge joystick;
  if(game->id!=MASTER && uint32_t(now-sampled)>=cfg::SAMPLE_MS) {
    sampled=now;
    if(game->active && (joystickEpoch!=game->epoch || joystickSequence!=game->seq)) {
      joystick=JoystickEdge(); joystickEpoch=game->epoch; joystickSequence=game->seq;
    }
    int x=analogRead(cfg::JOY_X), y=analogRead(cfg::JOY_Y);
    int direction=joystick.update(x,y,game->active,now); if(direction) game->move(direction,now);
    bool rail=x<20 || x>4075 || y<20 || y>4075;
    if(!rail) { railSince=now; railWarned=false; }
    else if(!railWarned && uint32_t(now-railSince)>cfg::STUCK_MS) {
      railWarned=true; eventLine("ERROR","Joystick held at rail; check wiring or recenter",game->id,0);
      Packet fault; fault.type=Type::FAULT; fault.from=game->id; fault.to=MASTER; fault.fromBoot=game->boot;
      fault.toBoot=game->peerBoot[MASTER]; fault.epoch=game->epoch; fault.value=2; radio::sendPacket(fault);
    }
  }
  if(uint32_t(now-lastStatus)>=cfg::STATUS_MS) { lastStatus=now; statusLine(); }
  if(game->fatal) setLEDColor(true,false,false);
  else if(game->paused) setLEDColor((now/250)%2,false,false);
  else if(game->active) setLEDColor(false,true,false);
  else if(int32_t(inputErrorUntil-now)>0) setLEDColor(true,false,false);
  else if(game->state==State::PASSING || game->state==State::STAGED || game->state==State::RESETTING) setLEDColor(false,false,(now/350)%2);
  else setLEDColor(false,false,false);
  // Apply local surrender/reset to the physical LED before transmitting any ACK.
  radio::pump();
  delay(1);
}
