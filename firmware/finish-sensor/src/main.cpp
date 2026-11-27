#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp_arduino_version.h>
#include "finish_sensor.hpp"
#include "status_light.hpp"
#ifndef JNS_SYNC_BENCH
#define JNS_SYNC_BENCH 0
#endif
// Optional single status NeoPixel (C6 build). RGBW parts such as SK6812 need JNS_PIXEL_RGBW=1.
#ifdef JNS_PIXEL
#include <Adafruit_NeoPixel.h>
#ifndef JNS_PIXEL_RGBW
#define JNS_PIXEL_RGBW 0
#endif
#ifndef JNS_PIXEL_BRIGHTNESS
#define JNS_PIXEL_BRIGHTNESS 48
#endif
Adafruit_NeoPixel pixel(1,JNS_PIXEL,(JNS_PIXEL_RGBW?NEO_GRBW:NEO_GRB)+NEO_KHZ800);
#endif
using namespace jns;
static_assert(JNS_LANE>=1 && JNS_LANE<=4,"Finish lane must be 1–4");
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
BeamWatch watch;
bool rawBroken=true;
LightView shown;
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
void onPacket(const uint8_t* mac,const uint8_t* data,int length) {
  const uint64_t at=nowUs();
  if (length!=int(kPacketSize)) return;
  Input r; r.at=at; memcpy(r.mac,mac,6); memcpy(r.bytes,data,kPacketSize);
  if (xQueueSend(inbox,&r,0)!=pdTRUE) {
    portENTER_CRITICAL(&radioLock); lost=true; portEXIT_CRITICAL(&radioLock);
  }
}
void sendDone() {
  // Status/Result are repeated; transport acknowledgement is not application receipt.
  portENTER_CRITICAL(&radioLock); busy=false; portEXIT_CRITICAL(&radioLock);
}
// Arduino-ESP32 3.x (needed for the C6) changed the ESP-NOW callback signatures.
#if ESP_ARDUINO_VERSION_MAJOR>=3
void received(const esp_now_recv_info_t* info,const uint8_t* data,int length) { onPacket(info->src_addr,data,length); }
#if ESP_IDF_VERSION>=ESP_IDF_VERSION_VAL(5,5,0)
void sent(const wifi_tx_info_t*,esp_now_send_status_t) { sendDone(); }
#else
void sent(const uint8_t*,esp_now_send_status_t) { sendDone(); }
#endif
#else
void received(const uint8_t* mac,const uint8_t* data,int length) { onPacket(mac,data,length); }
void sent(const uint8_t*,esp_now_send_status_t) { sendDone(); }
#endif
// 38 kHz, 50% duty IR carrier on the LEDC hardware. False if the frequency is not met.
bool startCarrier() {
#if ESP_ARDUINO_VERSION_MAJOR>=3
  if (!ledcAttach(JNS_IR_TX,38000,8)) return false;
  const uint32_t hz=ledcReadFreq(JNS_IR_TX);
  if (hz<37500 || hz>38500) return false;
  return ledcWrite(JNS_IR_TX,128);
#else
  const double hz=ledcSetup(0,38000,8);
  ledcAttachPin(JNS_IR_TX,0);
  if (hz<37500 || hz>38500) return false;
  ledcWrite(0,128); return true;
#endif
}
void showLight(const LightView& v) {
#ifdef JNS_PIXEL
  if (v.colour==shown.colour && v.lit==shown.lit) return;  // Write only on change.
  shown=v;
  const uint8_t b=JNS_PIXEL_BRIGHTNESS;
  uint32_t c=0;
  if (v.lit) switch (v.colour) {
    case Light::Red: c=pixel.Color(b,0,0); break;
    case Light::Amber: c=pixel.Color(b,uint8_t(b*45/100),0); break;
    case Light::Green: c=pixel.Color(0,b,0); break;
    case Light::Blue: c=pixel.Color(0,0,b); break;
    case Light::White: c=JNS_PIXEL_RGBW?pixel.Color(0,0,0,b):pixel.Color(b*6/10,b*6/10,b*6/10); break;
    case Light::Off: break;
  }
  pixel.setPixelColor(0,c); pixel.show();
#else
  (void)v;
#endif
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
#ifdef JNS_PIXEL
  pixel.begin(); pixel.clear(); pixel.show();
#endif
  uint32_t boot=esp_random(); if (!boot) boot=1;
  static FinishSensor instance(JNS_SYSTEM_ID,boot,JNS_LANE); sensor=&instance;
  inbox=xQueueCreate(48,sizeof(Input));
  if (!inbox) { sensor->hardwareFault(); return; }
  WiFi.setAutoReconnect(false); WiFi.mode(WIFI_STA); WiFi.disconnect(); WiFi.setSleep(false);
  esp_now_peer_info_t peer{}; memcpy(peer.peer_addr,broadcastMac,6);
  peer.channel=JNS_RADIO_CHANNEL; peer.ifidx=WIFI_IF_STA; peer.encrypt=false;
  radioReady=esp_wifi_set_channel(JNS_RADIO_CHANNEL,WIFI_SECOND_CHAN_NONE)==ESP_OK &&
      esp_now_init()==ESP_OK && esp_now_register_recv_cb(received)==ESP_OK &&
      esp_now_register_send_cb(sent)==ESP_OK && esp_now_add_peer(&peer)==ESP_OK;
  if (!radioReady) sensor->hardwareFault();
  if (!JNS_SYNC_BENCH && radioReady && !startCarrier()) sensor->hardwareFault();
  rawBroken=digitalRead(JNS_IR_RX)!=LOW;
  sensor->edge(rawBroken,nowUs());
  attachInterrupt(digitalPinToInterrupt(JNS_IR_RX),beamEdge,CHANGE);
  Serial.println(JNS_SYNC_BENCH?"Finish GPIO bench: LIVE radio, IR disabled":"Finish optical prototype");
}
void loop() {
  portENTER_CRITICAL(&radioLock); bool dropped=lost; lost=false; portEXIT_CRITICAL(&radioLock);
  Input input;
  while (!dropped && inbox && xQueueReceive(inbox,&input,0)==pdTRUE) {
    // A backlog over 100 ms means the loop stalled: discard it rather than apply it late.
    if (nowUs()-input.at>100000) { dropped=true; break; }
    if (input.edge) {
      sensor->edge(input.broken,input.at);
      rawBroken=input.broken;
      if (input.broken) watch.broke(input.at);
    } else {
      Packet p;
      if (decode(input.bytes,sizeof(input.bytes),p) && p.system==JNS_SYSTEM_ID) sensor->receive(input.mac,p,input.at);
    }
  }
  // Void the attempt only; the unit recovers on ST-1's CANCEL or Retired heartbeat.
  if (dropped && inbox) {
    xQueueReset(inbox); rawBroken=digitalRead(JNS_IR_RX)!=LOW; sensor->overflow(rawBroken,nowUs());
  }
  const uint64_t now=nowUs(); sensor->tick(now);
  digitalWrite(JNS_ALIGN_LED,sensor->clear(now)?HIGH:LOW); // Beam clear only, not optical margin.
  showLight(statusLight(sensor->faulted(),sensor->state(),rawBroken,sensor->clear(now),
    watch.unsteady(now),sensor->ready(now),now));
  static FinishState previous=FinishState::Idle;
  const bool changed=sensor->state()!=previous;
  if (changed || now-lastStatus>=500000) {
    if (transmit(sensor->status(now))) { lastStatus=now; previous=sensor->state(); }
  }
  Packet result;
  if (sensor->result(now,result) && (result.attempt!=resultAttempt || now-lastResult>=500000)) {
    if (transmit(result)) {
      if (result.attempt!=resultAttempt && Serial.availableForWrite()>80)
        Serial.printf("FN-%u attempt=%lu elapsed_us=%llu (unvalidated)\n",unsigned(JNS_LANE),(unsigned long)result.attempt,(unsigned long long)result.value);
      resultAttempt=result.attempt; lastResult=now;
    }
  }
  delay(1);
}
