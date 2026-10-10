#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <MD_MAX72xx.h>
#include "lane_display.hpp"
#include "display_text.hpp"
#ifndef JNS_DISPLAY_BENCH
#define JNS_DISPLAY_BENCH 0
#endif
static_assert(JNS_LANE>=1 && JNS_LANE<=4,"Invalid display lane");
static_assert(JNS_RADIO_CHANNEL>=1 && JNS_RADIO_CHANNEL<=13,"Invalid radio channel");
static_assert(JNS_MATRIX_INTENSITY>=0 && JNS_MATRIX_INTENSITY<=15,"Invalid intensity");
using namespace jns;
// Software SPI permits explicit pin allocation. FC16 is a prototype module choice.
MD_MAX72XX matrix(MD_MAX72XX::FC16_HW,JNS_MATRIX_DIN,JNS_MATRIX_CLK,JNS_MATRIX_CS,4);
LaneDisplay display(JNS_SYSTEM_ID,JNS_LANE);
struct Received { uint8_t mac[6], bytes[kPacketSize]; uint64_t at; };
QueueHandle_t inbox=nullptr;
portMUX_TYPE inboxLock=portMUX_INITIALIZER_UNLOCKED;
bool lost=false;
uint64_t nowUs() { return uint64_t(esp_timer_get_time()); }
void received(const uint8_t* mac,const uint8_t* bytes,int size) {
  const uint64_t at=nowUs(); // Capture before decoding or copying for the common timebase.
  if (size!=int(kPacketSize)) return;
  Received r; r.at=at; memcpy(r.mac,mac,6); memcpy(r.bytes,bytes,kPacketSize);
  if (xQueueSend(inbox,&r,0)!=pdTRUE) {
    portENTER_CRITICAL(&inboxLock); lost=true; portEXIT_CRITICAL(&inboxLock);
  }
}
void paint(const char* text) {
  static char previous[8]={};
  if (!strcmp(previous,text)) return;
  snprintf(previous,sizeof(previous),"%s",text);
  // Explicit fixed-width numeric font ensures 30.000 fits, independent of library fonts.
  static const uint8_t digits[10][5]={
    {0x3e,0x51,0x49,0x45,0x3e},{0x00,0x42,0x7f,0x40,0x00},
    {0x62,0x51,0x49,0x49,0x46},{0x22,0x41,0x49,0x49,0x36},
    {0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
    {0x3c,0x4a,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1e}
  };
  uint8_t columns[32]={}, count=0;
  for (const char* c=text;*c;++c) {
    uint8_t glyph[8]={}; uint8_t width;
    if (*c>='0' && *c<='9') { width=5; memcpy(glyph,digits[*c-'0'],5); }
    else if (*c=='.') { width=1; glyph[0]=0x40; }
    else width=matrix.getChar(*c,sizeof(glyph),glyph);
    if (count && count<32) columns[count++]=0;
    for (uint8_t i=0;i<width && count<32;++i) columns[count++]=glyph[i];
  }
  matrix.control(MD_MAX72XX::UPDATE,MD_MAX72XX::OFF); matrix.clear();
  const uint8_t left=(32-count)/2;
  for (uint8_t i=0;i<count;++i) matrix.setColumn(31-left-i,columns[i]);
  matrix.control(MD_MAX72XX::UPDATE,MD_MAX72XX::ON);
}
void setup() {
  Serial.begin(115200); matrix.begin(); matrix.control(MD_MAX72XX::INTENSITY,JNS_MATRIX_INTENSITY);
  paint("----");
  if (JNS_DISPLAY_BENCH) { WiFi.mode(WIFI_OFF); Serial.println("BENCH: display demo, radio disabled"); return; }
  inbox=xQueueCreate(32,sizeof(Received));
  WiFi.setAutoReconnect(false); WiFi.mode(WIFI_STA); WiFi.disconnect(); WiFi.setSleep(false);
  if (!inbox || esp_wifi_set_channel(JNS_RADIO_CHANNEL,WIFI_SECOND_CHAN_NONE)!=ESP_OK ||
      esp_now_init()!=ESP_OK || esp_now_register_recv_cb(received)!=ESP_OK) display.hardwareFault();
  Serial.printf("LN-%d receive-only prototype\n",JNS_LANE);
}
void loop() {
  if (JNS_DISPLAY_BENCH) {
    const char* frames[]={"TEST","1.750","12.345","30.000","DNF","ERR","----"};
    paint(frames[(nowUs()/3000000)%7]); delay(1); return;
  }
  display.tick(nowUs());
  portENTER_CRITICAL(&inboxLock); const bool overflow=lost; lost=false; portEXIT_CRITICAL(&inboxLock);
  if (overflow) {
    // Drop the entire batch: a lost CANCEL may precede a queued Result or Reveal.
    xQueueReset(inbox); display.overflow();
  }
  Received r;
  while (inbox && xQueueReceive(inbox,&r,0)==pdTRUE) {
    Packet p;
    if (decode(r.bytes,sizeof(r.bytes),p) && p.system==JNS_SYSTEM_ID) {
      if (nowUs()-r.at>100000) display.overflow();
      else display.receive(r.mac,p,r.at);
    }
  }
  const uint64_t now=nowUs(); display.tick(now);
  char text[8]; displayText(display.state(),display.elapsed(),display.synced(now),text); paint(text);
  delay(1);
}
