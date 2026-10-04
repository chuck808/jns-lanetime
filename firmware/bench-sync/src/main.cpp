// Bench sync test: measures timebase agreement between units on real radios.
// Output lines (CSV over USB serial):
//   EDGE,<seq>,<st1_us>            heartbeat board: true ST-1 time of each pulse
//   EDGE,<seq>,<st1_us>,<ready>    unit: the same pulse, converted to ST-1 time
//   EDGE,<seq>,FAIL,<reason>       unit: pulse could not be converted
//   TB,<ready>,<kept>,<spread_us>,<drift_ppm>,<samples>   unit: timebase health, every 2 s
// tools/bench_sync.py pairs the lines from all three boards and reports the differences.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "lanetime/protocol.hpp"
#include "lanetime/timebase.hpp"
using namespace jns;

static_assert(JNS_RADIO_CHANNEL>=1 && JNS_RADIO_CHANNEL<=13,"Invalid Wi-Fi channel");
const uint8_t broadcastMac[6]={0xff,0xff,0xff,0xff,0xff,0xff};
uint64_t nowUs() { return uint64_t(esp_timer_get_time()); }
uint32_t seq=0;

bool startRadio() {
  WiFi.mode(WIFI_STA); WiFi.disconnect(); WiFi.setSleep(false);
  esp_now_peer_info_t peer{}; memcpy(peer.peer_addr,broadcastMac,6);
  peer.channel=JNS_RADIO_CHANNEL; peer.ifidx=WIFI_IF_STA; peer.encrypt=false;
  return esp_wifi_set_channel(JNS_RADIO_CHANNEL,WIFI_SECOND_CHAN_NONE)==ESP_OK &&
    esp_now_init()==ESP_OK && esp_now_add_peer(&peer)==ESP_OK;
}

#if JNS_BENCH_HEARTBEAT
// ---- Stand-in ST-1 ----------------------------------------------------------
constexpr uint64_t kBeatUs=500000, kPulseEveryUs=1370000, kPulseLowUs=10000;
uint64_t sessionId=0, lastBeat=0, nextPulse=0, pulseEnd=0;
uint32_t bootId=0;

void setup() {
  Serial.begin(115200);
  pinMode(SYNC_PIN,OUTPUT); digitalWrite(SYNC_PIN,HIGH);
  do sessionId=(uint64_t(esp_random())<<32)|esp_random(); while (!sessionId);
  do bootId=esp_random(); while (!bootId);
  if (!startRadio()) { for (;;) { Serial.println("FATAL,radio"); delay(1000); } }
  nextPulse=nowUs()+10000000;  // Give the units time to become Ready.
}
void loop() {
  uint64_t now=nowUs();
  if (now-lastBeat>=kBeatUs) {
    Packet p; p.kind=Kind::Status; p.role=Role::Controller; p.lane=1; p.system=JNS_SYSTEM_ID;
    p.session=sessionId; p.flags=Ready; p.boot=bootId;
    uint8_t b[kPacketSize]; p.value=nowUs(); encode(p,b);  // Stamp as late as possible.
    esp_now_send(broadcastMac,b,sizeof(b)); lastBeat=now;
  }
  if (!pulseEnd && now>=nextPulse) {
    const uint64_t t=nowUs(); digitalWrite(SYNC_PIN,LOW);  // Falling edge = the event.
    Serial.printf("EDGE,%lu,%llu\n",(unsigned long)++seq,(unsigned long long)t);
    pulseEnd=t+kPulseLowUs; nextPulse+=kPulseEveryUs;
  }
  if (pulseEnd && nowUs()>=pulseEnd) { digitalWrite(SYNC_PIN,HIGH); pulseEnd=0; }
}

#else
// ---- Test unit ----------------------------------------------------------------
struct Sample { uint64_t local, remote, session; };
QueueHandle_t samples=nullptr;
volatile uint64_t edgeAt=0;
volatile bool edgePending=false;
volatile uint32_t dropped=0;
Timebase tb;
struct Held { uint32_t seq; uint64_t local; bool ready; };
Held held[8]; uint8_t heldCount=0;
uint64_t lastReport=0; uint32_t sampleCount=0;

void IRAM_ATTR edgeIsr() {
  const uint64_t t=nowUs();
  if (!edgePending) { edgeAt=t; edgePending=true; }
}
void received(const uint8_t*,const uint8_t* data,int len) {
  const uint64_t local=nowUs();  // First statement: the timebase sample time.
  Packet p;
  if (len<0 || !decode(data,size_t(len),p) || p.system!=JNS_SYSTEM_ID ||
      p.kind!=Kind::Status || p.role!=Role::Controller) return;
  const Sample s{local,p.value,p.session};
  if (xQueueSend(samples,&s,0)!=pdTRUE) ++dropped;
}
void setup() {
  Serial.begin(115200);
  samples=xQueueCreate(16,sizeof(Sample));
  pinMode(SYNC_PIN,INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(SYNC_PIN),edgeIsr,FALLING);
  if (!samples || !startRadio() || esp_now_register_recv_cb(received)!=ESP_OK)
    for (;;) { Serial.println("FATAL,radio"); delay(1000); }
}
void loop() {
  Sample s;
  while (xQueueReceive(samples,&s,0)==pdTRUE) { tb.sample(s.session,s.local,s.remote); ++sampleCount; }
  const uint64_t now=nowUs();
  if (edgePending) {
    noInterrupts(); const uint64_t at=edgeAt; edgePending=false; interrupts();
    ++seq;
    if (heldCount<8) held[heldCount++]={seq,at,tb.ready(now)};
    else Serial.printf("EDGE,%lu,FAIL,held-full\n",(unsigned long)seq);
  }
  for (uint8_t i=0;i<heldCount;) {
    const Held& h=held[i]; uint64_t st1=0;
    const bool expired=now-h.local>10000000;
    if (tb.interpolable(h.local) && tb.toRemote(h.local,st1))
      Serial.printf("EDGE,%lu,%llu,%d\n",(unsigned long)h.seq,(unsigned long long)st1,h.ready?1:0);
    else if (expired) Serial.printf("EDGE,%lu,FAIL,not-interpolable\n",(unsigned long)h.seq);
    else { ++i; continue; }
    held[i]=held[--heldCount];
  }
  if (now-lastReport>=2000000) {
    lastReport=now;
    Serial.printf("TB,%d,%u,%.1f,%.2f,%lu,%lu\n",tb.ready(now)?1:0,unsigned(tb.kept()),tb.spread(),
      tb.drift()*1e6,(unsigned long)sampleCount,(unsigned long)dropped);
  }
}
#endif
