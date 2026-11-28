#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp_arduino_version.h>
#include "lane_display.hpp"
#include "display_text.hpp"
#ifndef JNS_DISPLAY_BENCH
#define JNS_DISPLAY_BENCH 0
#endif
#ifndef JNS_WS2812
#define JNS_WS2812 0
#endif
#ifndef JNS_STATUS_LED
#define JNS_STATUS_LED -1
#endif
static_assert(JNS_LANE>=1 && JNS_LANE<=4,"Invalid display lane");
static_assert(JNS_RADIO_CHANNEL>=1 && JNS_RADIO_CHANNEL<=13,"Invalid radio channel");
using namespace jns;
#if JNS_WS2812
#include <Adafruit_NeoPixel.h>
// 8x32 addressable RGB panel, wired as 32 columns of 8 pixels. Most flexible panels run each
// column in alternate directions (serpentine); flip flags correct panel orientation.
static_assert(JNS_MATRIX_BRIGHTNESS>=1 && JNS_MATRIX_BRIGHTNESS<=64,"Brightness capped for USB/5 V supply safety");
Adafruit_NeoPixel matrix(256,JNS_MATRIX_DIN,NEO_GRB+NEO_KHZ800);
uint16_t pixel(uint8_t x,uint8_t y) {
  if (JNS_MATRIX_FLIP_X) x=31-x;
  if (JNS_MATRIX_FLIP_Y) y=7-y;
  return uint16_t(x)*8+((JNS_MATRIX_SERPENTINE && (x&1))?7-y:y);
}
// 5x7 columns, bit 0 at the top, for every letter displayText(), speedText() and the bench
// demo produce.
uint8_t letter(char c,uint8_t* glyph) {
  static const struct { char c; uint8_t g[5]; } font[]={
    {'-',{0x08,0x08,0x08,0x08,0x08}},{'/',{0x20,0x10,0x08,0x04,0x02}},
    {'A',{0x7c,0x12,0x11,0x12,0x7c}},{'C',{0x3e,0x41,0x41,0x41,0x22}},
    {'D',{0x7f,0x41,0x41,0x41,0x3e}},{'E',{0x7f,0x49,0x49,0x49,0x41}},
    {'F',{0x7f,0x09,0x09,0x09,0x01}},{'H',{0x7f,0x08,0x08,0x08,0x7f}},
    {'I',{0x00,0x41,0x7f,0x41,0x00}},{'K',{0x7f,0x08,0x14,0x22,0x41}},
    {'L',{0x7f,0x40,0x40,0x40,0x40}},{'M',{0x7f,0x02,0x0c,0x02,0x7f}},
    {'N',{0x7f,0x04,0x08,0x10,0x7f}},{'O',{0x3e,0x41,0x41,0x41,0x3e}},
    {'R',{0x7f,0x09,0x19,0x29,0x46}},{'S',{0x46,0x49,0x49,0x49,0x31}},
    {'T',{0x01,0x01,0x7f,0x01,0x01}},{'Y',{0x07,0x08,0x70,0x08,0x07}}
  };
  for (const auto& f:font) if (f.c==c) { memcpy(glyph,f.g,5); return 5; }
  return 0;
}
#else
#include <MD_MAX72xx.h>
static_assert(JNS_MATRIX_INTENSITY>=0 && JNS_MATRIX_INTENSITY<=15,"Invalid intensity");
// Software SPI permits explicit pin allocation. FC16 is a prototype module choice.
MD_MAX72XX matrix(MD_MAX72XX::FC16_HW,JNS_MATRIX_DIN,JNS_MATRIX_CLK,JNS_MATRIX_CS,4);
#endif
LaneDisplay display(JNS_SYSTEM_ID,JNS_LANE);
struct Received { uint8_t mac[6], bytes[kPacketSize]; uint64_t at; };
QueueHandle_t inbox=nullptr;
portMUX_TYPE inboxLock=portMUX_INITIALIZER_UNLOCKED;
bool lost=false;
uint64_t nowUs() { return uint64_t(esp_timer_get_time()); }
#if ESP_ARDUINO_VERSION_MAJOR>=3
void received(const esp_now_recv_info_t* info,const uint8_t* bytes,int size) {
  const uint64_t at=nowUs(); // Capture before decoding or copying for the common timebase.
  const uint8_t* mac=info->src_addr;
#else
void received(const uint8_t* mac,const uint8_t* bytes,int size) {
  const uint64_t at=nowUs(); // Capture before decoding or copying for the common timebase.
#endif
  if (size!=int(kPacketSize)) return;
  Received r; r.at=at; memcpy(r.mac,mac,6); memcpy(r.bytes,bytes,kPacketSize);
  if (xQueueSend(inbox,&r,0)!=pdTRUE) {
    portENTER_CRITICAL(&inboxLock); lost=true; portEXIT_CRITICAL(&inboxLock);
  }
}
// Explicit fixed-width numeric font ensures 30.000 fits, independent of library fonts.
// Letters (KM/H, DNF and so on) come from the library's font.
uint8_t layout(const char* text,uint8_t (&columns)[96]) {
  static const uint8_t digits[10][5]={
    {0x3e,0x51,0x49,0x45,0x3e},{0x00,0x42,0x7f,0x40,0x00},
    {0x62,0x51,0x49,0x49,0x46},{0x22,0x41,0x49,0x49,0x36},
    {0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
    {0x3c,0x4a,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1e}
  };
  uint8_t count=0;
  for (const char* c=text;*c;++c) {
    uint8_t glyph[8]={}; uint8_t width;
    if (*c>='0' && *c<='9') { width=5; memcpy(glyph,digits[*c-'0'],5); }
    else if (*c=='.') { width=1; glyph[0]=0x40; }
    else if (*c==' ') { width=2; }
#if JNS_WS2812
    else width=letter(*c,glyph);
#else
    else width=matrix.getChar(*c,sizeof(glyph),glyph);
#endif
    if (count && count<sizeof(columns)) columns[count++]=0;
    for (uint8_t i=0;i<width && count<sizeof(columns);++i) columns[count++]=glyph[i];
  }
  return count;
}
// Draws text with its left edge at column left (may be off either side), or centred.
void paint(const char* text,bool centred=true,int16_t left=0) {
  static char previous[16]={};
  static int16_t previousLeft=-999;
  if (centred) left=-999;
  if (!strcmp(previous,text) && previousLeft==left) return;
  snprintf(previous,sizeof(previous),"%s",text); previousLeft=left;
  uint8_t columns[96]={};
  const uint8_t count=layout(text,columns);
  if (centred) left=int16_t((32-int16_t(count>32?32:count))/2);
#if JNS_WS2812
  matrix.clear();
  const uint32_t colour=matrix.Color(255,110,0); // Amber; brightness is scaled by setBrightness.
  for (uint8_t i=0;i<count;++i) {
    const int16_t x=left+i;
    if (x>=0 && x<32)
      for (uint8_t y=0;y<8;++y) if (columns[i]&(1<<y)) matrix.setPixelColor(pixel(uint8_t(x),y),colour);
  }
  matrix.show();
#else
  matrix.control(MD_MAX72XX::UPDATE,MD_MAX72XX::OFF); matrix.clear();
  for (uint8_t i=0;i<count;++i) {
    const int16_t x=left+i;
    if (x>=0 && x<32) matrix.setColumn(31-x,columns[i]);
  }
  matrix.control(MD_MAX72XX::UPDATE,MD_MAX72XX::ON);
#endif
}
// Optional on-board LED: lit while synchronised to ST-1. Useful without a panel attached.
void statusLed(bool on) {
  if (JNS_STATUS_LED<0) return;
  static int previous=-1;
  if (int(on)!=previous) { digitalWrite(JNS_STATUS_LED,on?HIGH:LOW); previous=on; }
}
// The time held still, with the speed scrolling across between holds.
void show(DisplayState state,uint64_t elapsed,uint8_t metres,uint64_t sinceReveal,bool synced) {
  char text[8]; displayText(state,elapsed,synced,text);
  char speed[16]; uint8_t columns[96];
  const bool haveSpeed=speedText(state,elapsed,metres,speed);
  const Frame f=resultFrame(sinceReveal,haveSpeed?layout(speed,columns):0,haveSpeed);
  if (f.scroll) paint(speed,false,f.left); else paint(text);
}
void setup() {
  Serial.begin(115200);
  if (JNS_STATUS_LED>=0) { pinMode(JNS_STATUS_LED,OUTPUT); digitalWrite(JNS_STATUS_LED,LOW); }
#if JNS_WS2812
  matrix.begin(); matrix.setBrightness(JNS_MATRIX_BRIGHTNESS);
#else
  matrix.begin(); matrix.control(MD_MAX72XX::INTENSITY,JNS_MATRIX_INTENSITY);
#endif
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
    // Static frames, then a revealed 30 m time with its scrolling speed.
    const char* frames[]={"TEST","01.750","12.345","30.000","DNF","ERR","----"};
    const uint64_t t=nowUs()%33000000;
    if (t<21000000) paint(frames[t/3000000]);
    else show(DisplayState::Time,2004000,30,t-21000000,true);
    delay(1); return;
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
  const uint64_t since=now>display.revealedAt()?now-display.revealedAt():0;
  show(display.state(),display.elapsed(),display.distance(),since,display.synced(now));
  statusLed(display.synced(now));
  delay(1);
}
