#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "finish_sensor.hpp"
#ifndef JNS_SYNC_BENCH
#define JNS_SYNC_BENCH 0
#endif
using namespace jns;
static_assert(JNS_RADIO_CHANNEL>=1 && JNS_RADIO_CHANNEL<=13,"Invalid radio channel");
constexpr uint8_t broadcastMac[]={255,255,255,255,255,255};
struct Input {
  bool edge=false,broken=false;
  uint64_t at=0;
  uint8_t mac[6]={},bytes[kPacketSize]={};
};
QueueHandle_t inbox=nullptr;
portMUX_TYPE radioLock=portMUX_INITIALIZER_UNLOCKED;
volatile bool lost=false,busy=false;
FinishSensor* sensor=nullptr;
bool radioReady=false;
uint64_t lastStatus=0,lastResult=0;
uint32_t resultAttempt=0;
uint64_t nowUs() { return uint64_t(esp_timer_get_time()); }
void IRAM_ATTR beamEdge() {
  Input e; e.edge=true; e.at=uint64_t(esp_timer_get_time());
  e.broken=gpio_get_level(gpio_num_t(JNS_IR_RX))!=0;
  BaseType_t wake=pdFALSE;
  if (xQueueSendFromISR(inbox,&e,&wake)!=pdTRUE) {
    portENTER_CRITICAL_ISR(&radioLock); lost=true; portEXIT_CRITICAL_ISR(&radioLock);
  }
  if (wake) portYIELD_FROM_ISR();
}
void received(const uint8_t* mac,const uint8_t* data,int length) {
  const uint64_t at=nowUs();
  if (length!=int(kPacketSize)) return;
  Input r; r.at=at; memcpy(r.mac,mac,6); memcpy(r.bytes,data,kPacketSize);
  if (xQueueSend(inbox,&r,0)!=pdTRUE) {
    portENTER_CRITICAL(&radioLock); lost=true; portEXIT_CRITICAL(&radioLock);
  }
}
void sent(const uint8_t*,esp_now_send_status_t) {
  // Status/Result are repeated; transport acknowledgement is not application receipt.
  portENTER_CRITICAL(&radioLock); busy=false; portEXIT_CRITICAL(&radioLock);
}
bool transmit(const Packet& p) {
  if (!radioReady) return false;
  portENTER_CRITICAL(&radioLock); const bool occupied=busy; if (!occupied) busy=true; portEXIT_CRITICAL(&radioLock);
  if (occupied) return false;
  uint8_t bytes[kPacketSize]; encode(p,bytes);
  if (esp_now_send(broadcastMac,bytes,sizeof(bytes))!=ESP_OK) {
    portENTER_CRITICAL(&radioLock); busy=false; portEXIT_CRITICAL(&radioLock); return false;
  }
  return true;
}
void setup() {
  Serial.begin(115200);
  pinMode(JNS_IR_TX,OUTPUT); digitalWrite(JNS_IR_TX,LOW);
  pinMode(JNS_IR_RX,INPUT_PULLUP); pinMode(JNS_ALIGN_LED,OUTPUT);
  uint32_t boot=esp_random(); if (!boot) boot=1;
  static FinishSensor instance(JNS_SYSTEM_ID,boot); sensor=&instance;
  inbox=xQueueCreate(48,sizeof(Input));
  if (!inbox) { sensor->hardwareFault(); return; }
  WiFi.setAutoReconnect(false); WiFi.mode(WIFI_STA); WiFi.disconnect(); WiFi.setSleep(false);
  esp_now_peer_info_t peer{}; memcpy(peer.peer_addr,broadcastMac,6);
  peer.channel=JNS_RADIO_CHANNEL; peer.ifidx=WIFI_IF_STA; peer.encrypt=false;
  radioReady=esp_wifi_set_channel(JNS_RADIO_CHANNEL,WIFI_SECOND_CHAN_NONE)==ESP_OK &&
      esp_now_init()==ESP_OK && esp_now_register_recv_cb(received)==ESP_OK &&
      esp_now_register_send_cb(sent)==ESP_OK && esp_now_add_peer(&peer)==ESP_OK;
  if (!radioReady) sensor->hardwareFault();
  if (!JNS_SYNC_BENCH) {
    const double hz=ledcSetup(0,38000,8);
    ledcAttachPin(JNS_IR_TX,0);
    if (hz<37500 || hz>38500) sensor->hardwareFault();
    else if (radioReady) ledcWrite(0,128);
  }
  sensor->edge(digitalRead(JNS_IR_RX)!=LOW,nowUs());
  attachInterrupt(digitalPinToInterrupt(JNS_IR_RX),beamEdge,CHANGE);
  Serial.println(JNS_SYNC_BENCH?"FN-1 GPIO bench: LIVE radio, IR disabled":"FN-1 optical prototype");
}
void loop() {
  portENTER_CRITICAL(&radioLock); const bool overflow=lost; lost=false; portEXIT_CRITICAL(&radioLock);
  if (overflow) { xQueueReset(inbox); sensor->overflow(); }
  Input input;
  while (inbox && xQueueReceive(inbox,&input,0)==pdTRUE) {
    if (input.edge) {
      if (nowUs()-input.at>100000) sensor->overflow();
      else sensor->edge(input.broken,input.at);
    } else {
      Packet p;
      if (decode(input.bytes,sizeof(input.bytes),p) && p.system==JNS_SYSTEM_ID) {
        if (nowUs()-input.at>100000) sensor->overflow();
        else sensor->receive(input.mac,p,input.at);
      }
    }
  }
  const uint64_t now=nowUs(); sensor->tick(now);
  digitalWrite(JNS_ALIGN_LED,sensor->clear(now)?HIGH:LOW); // Beam clear only, not optical margin.
  static FinishState previous=FinishState::Idle;
  const bool changed=sensor->state()!=previous;
  if (changed || now-lastStatus>=500000) {
    if (transmit(sensor->status(now))) { lastStatus=now; previous=sensor->state(); }
  }
  Packet result;
  if (sensor->result(now,result) && (result.attempt!=resultAttempt || now-lastResult>=500000)) {
    if (transmit(result)) {
      if (result.attempt!=resultAttempt && Serial.availableForWrite()>80)
        Serial.printf("FN-1 attempt=%lu elapsed_us=%llu (unvalidated)\n",(unsigned long)result.attempt,(unsigned long long)result.value);
      resultAttempt=result.attempt; lastResult=now;
    }
  }
  delay(1);
}
