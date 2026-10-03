#include "radio.h"
#include "config.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_arduino_version.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#if ESP_ARDUINO_VERSION_MAJOR != 2
#error "Use Arduino-ESP32 2.0.17 (PlatformIO espressif32 6.9.0) for this verified build."
#endif
namespace radio {
struct Frame { uint8_t from; uint8_t bytes[ball::WIRE_SIZE]; };
static QueueHandle_t rx=nullptr, tx=nullptr;
static uint8_t selfID=0;
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static volatile bool busy=false;
static volatile uint32_t failCount=0, rejectCount=0, dropCount=0;
static void increment(volatile uint32_t& counter) { portENTER_CRITICAL(&mux); ++counter; portEXIT_CRITICAL(&mux); }
static void onSent(const uint8_t*, esp_now_send_status_t status) {
  portENTER_CRITICAL(&mux); if(status!=ESP_NOW_SEND_SUCCESS) ++failCount; busy=false; portEXIT_CRITICAL(&mux);
}
static void onReceived(const uint8_t* mac, const uint8_t* data, int len) {
  uint8_t from=ball::findPlayerByMAC(mac);
  if(!from || len!=int(ball::WIRE_SIZE)) { increment(rejectCount); return; }
  Frame f; f.from=from; memcpy(f.bytes,data,ball::WIRE_SIZE);
  if(xQueueSend(rx,&f,0)!=pdTRUE) increment(dropCount);
}
static bool addPeer(uint8_t id) {
  const auto* p=ball::findPlayerByID(id); if(!p) return false;
  if(esp_now_is_peer_exist(p->mac)) return true;
  esp_now_peer_info_t info={}; memcpy(info.peer_addr,p->mac,6);
  info.channel=cfg::WIFI_CHANNEL; info.ifidx=WIFI_IF_STA; info.encrypt=false;
  return esp_now_add_peer(&info)==ESP_OK && esp_now_is_peer_exist(p->mac);
}
bool begin(uint8_t self) {
  selfID=self;
  rx=xQueueCreate(40,sizeof(Frame)); tx=xQueueCreate(64,sizeof(ball::Packet));
  if(!rx || !tx || esp_wifi_set_channel(cfg::WIFI_CHANNEL,WIFI_SECOND_CHAN_NONE)!=ESP_OK || esp_now_init()!=ESP_OK) return false;
  if(esp_now_register_recv_cb(onReceived)!=ESP_OK || esp_now_register_send_cb(onSent)!=ESP_OK) return false;
  for(uint8_t id=1;id<=ball::COUNT;id++) if(id!=self && !addPeer(id)) return false;
  return true;
}
bool sendPacket(const ball::Packet& p) {
  static uint32_t testCount=0;
  if((cfg::DROP_TX_TYPE==uint8_t(p.type)) || (cfg::DROP_EVERY_N && ++testCount % cfg::DROP_EVERY_N == 0)) return true;
  if(!tx || xQueueSend(tx,&p,0)!=pdTRUE) { increment(dropCount); return false; }
  if(cfg::DUPLICATE_TX_TYPE==uint8_t(p.type) && xQueueSend(tx,&p,0)!=pdTRUE) increment(dropCount);
  return true;
}
void pump() {
  portENTER_CRITICAL(&mux); bool waiting=busy; portEXIT_CRITICAL(&mux);
  if(waiting) return;
  ball::Packet p; if(xQueueReceive(tx,&p,0)!=pdTRUE) return;
  uint8_t bytes[ball::WIRE_SIZE]; ball::encode(p,bytes);
  portENTER_CRITICAL(&mux); busy=true; portEXIT_CRITICAL(&mux);
  if(esp_now_send(ball::findPlayerByID(p.to)->mac,bytes,sizeof(bytes))!=ESP_OK) {
    portENTER_CRITICAL(&mux); busy=false; ++failCount; portEXIT_CRITICAL(&mux);
  }
}
bool receive(ball::Packet& p) {
  Frame f;
  while(rx && xQueueReceive(rx,&f,0)==pdTRUE) {
    if(ball::decode(f.bytes,sizeof(f.bytes),f.from,selfID,p)) return true;
    increment(rejectCount);
  }
  return false;
}
uint32_t failures() { portENTER_CRITICAL(&mux); uint32_t n=failCount; portEXIT_CRITICAL(&mux); return n; }
uint32_t rejected() { portENTER_CRITICAL(&mux); uint32_t n=rejectCount; portEXIT_CRITICAL(&mux); return n; }
uint32_t dropped() { portENTER_CRITICAL(&mux); uint32_t n=dropCount; portEXIT_CRITICAL(&mux); return n; }
}
