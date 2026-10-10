#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <Adafruit_ThinkInk.h>
#include <Adafruit_NeoPixel.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "controller.hpp"
#include "beam.hpp"
#include "button.hpp"
#ifndef JNS_BENCH
#define JNS_BENCH 0
#endif
#ifndef JNS_LEGACY_DISPLAY
#define JNS_LEGACY_DISPLAY 0
#endif
static_assert(JNS_RADIO_CHANNEL>=1 && JNS_RADIO_CHANNEL<=13,"Invalid Wi-Fi channel");
using namespace jns;
constexpr uint8_t txPin=10, rxPin=18, speakerPin=17, speakerEnable=16;
constexpr uint8_t irChannel=0, soundChannel=2; // Separate LEDC timers.
constexpr uint8_t buttons[]={BUTTON_A,BUTTON_B,BUTTON_C,BUTTON_D};
constexpr uint8_t broadcastMac[]={255,255,255,255,255,255};
#if JNS_LEGACY_DISPLAY
ThinkInk_290_Grayscale4_T5 display(7,6,8,-1,5,&SPI);
#else
ThinkInk_290_Grayscale4_EAAMFGN display(7,6,8,-1,5,&SPI);
#endif
Adafruit_NeoPixel pixels(4,1,NEO_GRB+NEO_KHZ800);
Preferences preferences;
Controller* controller=nullptr;
Beam beam;
uint64_t sessionId=0, lastHeartbeat=0, lastArmSend=0, lastPaint=0, toneUntil=0;
uint64_t lastPixels=0, cancelUntil=0, lastCancel=0, lastGoSend=0, lastStartSend=0;
uint64_t lastRevealSend=0, sentRevealAt=0;
uint32_t bootId=0;
bool dirty=true, radioReady=false, fatalHardware=false;
struct Received { uint8_t mac[6]; uint8_t data[kPacketSize]; uint64_t at; };
struct Edge { bool broken; uint64_t at; };
QueueHandle_t radioQueue=nullptr, edgeQueue=nullptr;
portMUX_TYPE radioLock=portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE edgeLock=portMUX_INITIALIZER_UNLOCKED;
volatile bool radioBusy=false, sendFailed=false, overflow=false, edgeOverflow=false;
uint64_t nowUs() { return uint64_t(esp_timer_get_time()); }

void received(const uint8_t* mac,const uint8_t* data,int size) {
  if (size!=int(kPacketSize)) return;
  Packet p;
  if (!decode(data,size,p) || p.system!=JNS_SYSTEM_ID) return;
  Received r; memcpy(r.mac,mac,6); memcpy(r.data,data,kPacketSize); r.at=nowUs();
  if (xQueueSend(radioQueue,&r,0)!=pdTRUE) {
    portENTER_CRITICAL(&radioLock); overflow=true; portEXIT_CRITICAL(&radioLock);
  }
}
void sent(const uint8_t*,esp_now_send_status_t status) {
  portENTER_CRITICAL(&radioLock);
  radioBusy=false;
  if (status!=ESP_NOW_SEND_SUCCESS) sendFailed=true;
  portEXIT_CRITICAL(&radioLock);
}
void IRAM_ATTR beamEdge() {
  Edge e{gpio_get_level(gpio_num_t(rxPin))!=0,uint64_t(esp_timer_get_time())};
  BaseType_t wake=pdFALSE;
  if (xQueueSendFromISR(edgeQueue,&e,&wake)!=pdTRUE) {
    portENTER_CRITICAL_ISR(&edgeLock); edgeOverflow=true; portEXIT_CRITICAL_ISR(&edgeLock);
  }
  if (wake) portYIELD_FROM_ISR();
}
Packet packet(Kind kind) {
  Packet p;
  p.kind=kind; p.system=JNS_SYSTEM_ID; p.session=sessionId; p.boot=bootId;
  p.attempt=controller->attempt; p.mode=controller->mode; p.mask=controller->mask();
  if (controller->state==State::Armed || controller->state==State::Countdown || controller->state==State::Running) p.flags|=jns::Armed;
  if (controller->state==State::Running) p.flags|=jns::Running;
  if (controller->state==State::Revealing || controller->state==State::Complete) p.flags|=jns::Complete;
  if (kind==Kind::Reveal) p.finishedMask=controller->finished;
  if (controller->state==State::Fault) p.flags|=jns::Fault;
  if (controller->mode==Mode::Standing || beam.clear(nowUs())) p.flags|=jns::Ready;
  return p;
}
bool transmit(Kind kind,uint64_t value=0) {
  if (JNS_BENCH) return true; // No radio traffic in the standalone demo.
  if (!radioReady) return false;
  portENTER_CRITICAL(&radioLock);
  const bool busy=radioBusy;
  if (!busy) radioBusy=true;
  portEXIT_CRITICAL(&radioLock);
  if (busy) return false; // Callers retry; timing values are timestamps, not arrival times.
  auto p=packet(kind); p.value=value;
  if (kind==Kind::Status) p.value=nowUs(); // Common timebase sample for every peer.
  uint8_t bytes[kPacketSize]; encode(p,bytes);
  if (esp_now_send(broadcastMac,bytes,sizeof(bytes))!=ESP_OK) {
    portENTER_CRITICAL(&radioLock); radioBusy=false; portEXIT_CRITICAL(&radioLock);
    return false;
  }
  return true;
}
// Countdown cues run from an esp_timer at scheduled ST-1 instants, independent of the
// main loop. ST-2..N do the same after converting GO to their own clocks.
struct Cue { uint64_t at; uint16_t hz; };
Cue cues[2*(kCueBeeps+1)];
uint8_t cueCount=0;
volatile uint8_t cueNext=0;
esp_timer_handle_t cueTimer=nullptr;
void armCue() {
  const int64_t wait=int64_t(cues[cueNext].at)-esp_timer_get_time();
  esp_timer_start_once(cueTimer,wait>0?uint64_t(wait):0);
}
void cueFire(void*) {
  const Cue& c=cues[cueNext];
  if (c.hz) { digitalWrite(speakerEnable,HIGH); ledcWriteTone(soundChannel,c.hz); }
  else { ledcWrite(soundChannel,0); digitalWrite(speakerEnable,LOW); }
  if (++cueNext<cueCount) armCue();
}
void stopCues() {
  if (cueTimer) esp_timer_stop(cueTimer);
  cueCount=cueNext=0; ledcWrite(soundChannel,0); digitalWrite(speakerEnable,LOW);
}
void scheduleCues(uint64_t goAt) {
  if (!cueTimer) return;
  stopCues(); uint8_t n=0;
  for (uint8_t i=kCueBeeps;i>0;--i) {
    const uint64_t at=goAt-i*kCueStepUs;
    cues[n++]={at,1000}; cues[n++]={at+120000,0};
  }
  cues[n++]={goAt,2000}; cues[n++]={goAt+400000,0};
  cueCount=n; cueNext=0; armCue();
}
void beep(uint16_t hz,uint32_t durationMs) {
  digitalWrite(speakerEnable,HIGH); ledcWriteTone(soundChannel,hz);
  toneUntil=nowUs()+uint64_t(durationMs)*1000;
}
const char* stateName(State state) {
  switch (state) {
    case State::Idle:return "IDLE"; case State::Preparing:return "PREPARING";
    case State::Armed:return "ARMED"; case State::Countdown:return "COUNTDOWN";
    case State::Running:return "RUNNING"; case State::Revealing:return "REVEALING";
    case State::Complete:return "COMPLETE";
    case State::Fault:return "INVALID";
  }
  return "?";
}
void paint() {
  // The blocking eInk driver must never run during an active attempt.
  if (controller->active()) return;
  display.clearBuffer(); display.setTextColor(EPD_BLACK); display.setTextWrap(false);
  display.setTextSize(2); display.setCursor(0,0);
  display.println(JNS_BENCH ? "JNS ST-1  BENCH" : "JNS LaneTime ST-1");
  display.setTextSize(1); display.setCursor(0,24);
  display.printf("%s | Lanes 1-%u | Attempt %lu\n",controller->mode==Mode::Flying?"FLYING":"STANDING",controller->lanes,(unsigned long)controller->attempt);
  display.setCursor(0,40); display.println(stateName(controller->state)); display.println(controller->reason);
  display.printf("Lane 1 beam: %s\n",beam.clear(nowUs())?"CLEAR":"BROKEN / settling");
  if (controller->state==State::Complete) {
    for (uint8_t i=0;i<controller->lanes;++i) {
      if (controller->finished&(1U<<i)) {
        const uint64_t rounded=(controller->results[i]+500)/1000;
        display.printf("L%u: %lu.%03lu s  ",i+1,(unsigned long)(rounded/1000),(unsigned long)(rounded%1000));
      } else display.printf("L%u: DNF  ",i+1);
      if (i%2) display.println();
    }
  } else {
    display.println("LIVE LEDs: green=armed blue=run red=invalid");
    display.println("Prototype - timing accuracy unverified");
  }
  display.setCursor(0,110); display.println("MODE    LANES    CANCEL    ARM / GO");
  display.display(); dirty=false; lastPaint=nowUs();
}
void saveSettings() {
  preferences.putUChar("mode",uint8_t(controller->mode)); preferences.putUChar("lanes",controller->lanes);
}
Button button[4];
void benchPeers(uint64_t now) {
  if (!JNS_BENCH) return;
  for (uint8_t lane=1;lane<=controller->lanes;++lane) for (Role role:{Role::Start,Role::Finish}) {
    if (role==Role::Start && lane==1) continue;
    auto p=packet(Kind::Status); p.role=role; p.lane=lane; p.boot=100+lane+10*uint8_t(role);
    p.flags=jns::Ready | (controller->active()?jns::Armed:0);
    if (controller->goAt) { p.flags|=jns::Cued; p.value=controller->goAt; }
    uint8_t mac[]={2,0,0,0,uint8_t(role),lane}; controller->observe(mac,p,now);
  }
}
void setup() {
  pinMode(txPin,OUTPUT); digitalWrite(txPin,LOW);
  pinMode(speakerEnable,OUTPUT); digitalWrite(speakerEnable,LOW);
  pinMode(21,OUTPUT); digitalWrite(21,LOW); // NeoPixel power.
  pixels.begin(); pixels.setBrightness(16); pixels.clear(); pixels.show();
  Serial.begin(115200); // Never wait for a USB host.
  for (auto pin:buttons) pinMode(pin,INPUT_PULLUP);
  pinMode(rxPin,INPUT_PULLUP); // Disconnected receiver reads broken.
  radioQueue=xQueueCreate(24,sizeof(Received)); edgeQueue=xQueueCreate(32,sizeof(Edge));
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_STA); WiFi.disconnect(); WiFi.setSleep(false);
  bootId=esp_random(); if (!bootId) bootId=1;
  sessionId=(uint64_t(esp_random())<<32)|esp_random(); if (!sessionId) sessionId=1;
  static Controller instance(JNS_SYSTEM_ID,sessionId); controller=&instance;
  preferences.begin("jns-st1",false);
  controller->mode=preferences.getUChar("mode",0)==1?Mode::Standing:Mode::Flying;
  controller->lanes=preferences.getUChar("lanes",1);
  if (controller->lanes<1 || controller->lanes>4) controller->lanes=1;
  if (!radioQueue || !edgeQueue) { controller->fail("Queue allocation failed"); fatalHardware=true; }
  if (!JNS_BENCH && !fatalHardware) {
    esp_now_peer_info_t peer{}; memcpy(peer.peer_addr,broadcastMac,6);
    peer.channel=JNS_RADIO_CHANNEL; peer.ifidx=WIFI_IF_STA; peer.encrypt=false;
    radioReady=esp_wifi_set_channel(JNS_RADIO_CHANNEL,WIFI_SECOND_CHAN_NONE)==ESP_OK &&
      esp_now_init()==ESP_OK && esp_now_register_recv_cb(received)==ESP_OK &&
      esp_now_register_send_cb(sent)==ESP_OK && esp_now_add_peer(&peer)==ESP_OK;
    if (!radioReady) { controller->fail("Radio setup failed"); fatalHardware=true; }
  }
  const double irHz=ledcSetup(irChannel,38000,8);
  ledcAttachPin(txPin,irChannel); ledcWrite(irChannel,0);
  ledcSetup(soundChannel,2000,8); ledcAttachPin(speakerPin,soundChannel);
  const esp_timer_create_args_t cueArgs={cueFire,nullptr,ESP_TIMER_TASK,"cues",false};
  if (esp_timer_create(&cueArgs,&cueTimer)!=ESP_OK) { controller->fail("Cue timer setup failed"); fatalHardware=true; }
  if (irHz<37500 || irHz>38500) { controller->fail("IR PWM setup failed"); fatalHardware=true; }
  SPI.begin(36,37,35,8); display.begin(THINKINK_MONO); display.setRotation(0);
  beam.edge(digitalRead(rxPin)!=LOW,nowUs());
  if (edgeQueue) attachInterrupt(digitalPinToInterrupt(rxPin),beamEdge,CHANGE);
  if (!fatalHardware) ledcWrite(irChannel,128);
  paint();
}
void loop() {
  uint64_t now=nowUs(); const State before=controller->state;
  if (toneUntil && now>=toneUntil) { ledcWrite(soundChannel,0); digitalWrite(speakerEnable,LOW); toneUntil=0; }
  if (radioQueue) {
    Received r;
    while (xQueueReceive(radioQueue,&r,0)==pdTRUE) {
      Packet p;
      if (nowUs()-r.at<=kPeerUs && decode(r.data,sizeof(r.data),p)) controller->observe(r.mac,p,r.at);
    }
  }
  if (edgeQueue) {
    Edge e;
    while (xQueueReceive(edgeQueue,&e,0)==pdTRUE) beam.edge(e.broken,e.at);
  }
  portENTER_CRITICAL(&edgeLock);
  const bool lostEdge=edgeOverflow; edgeOverflow=false;
  portEXIT_CRITICAL(&edgeLock);
  if (lostEdge) {
    if (controller->active()) controller->fail("Beam edge queue overflow");
    beam=Beam(); beam.edge(digitalRead(rxPin)!=LOW,nowUs());
  }
  now=nowUs(); beam.advance(now);
  uint64_t eventAt;
  if (beam.take(eventAt) && eventAt>=controller->since && controller->mode==Mode::Flying) {
    if (controller->state==State::Preparing) controller->fail("Beam broke while preparing");
    else if (controller->localStart(eventAt) && transmit(Kind::Start,eventAt)) lastStartSend=nowUs();
  }
  benchPeers(now); controller->tick(now,beam.clear(now));
  portENTER_CRITICAL(&radioLock);
  // Send failures are retried by the callers below; lost received packets are not recoverable.
  bool radioError=overflow; sendFailed=false; overflow=false;
  portEXIT_CRITICAL(&radioLock);
  if (radioError && controller->active()) controller->fail("Radio queue / send fault");
  bool pressed[4];
  for (int i=0;i<4;++i) pressed[i]=button[i].update(digitalRead(buttons[i])==LOW,now,
      i==3 && before==State::Running && controller->state==State::Running);
  const bool revealHeld=button[3].held(now);
  // CANCEL wins over simultaneous GO.
  if (pressed[2] && !fatalHardware) {
    controller->cancel(); cancelUntil=now+2000000; transmit(Kind::Cancel); dirty=true;
  } else if (!fatalHardware) {
    if (revealHeld) controller->reveal(now);
    if (!controller->active() && (pressed[0] || pressed[1])) {
      const bool retire=controller->state!=State::Idle;
      controller->cancel();
      if (retire) { cancelUntil=now+2000000; transmit(Kind::Cancel); }
      if (pressed[0]) controller->mode=controller->mode==Mode::Flying?Mode::Standing:Mode::Flying;
      if (pressed[1]) controller->lanes=controller->lanes%4+1;
      saveSettings(); dirty=true;
    }
    if (pressed[3]) {
      if (controller->state==State::Armed && controller->mode==Mode::Standing) {
        if (controller->countdown(now)) { stopCues(); lastGoSend=0; if (transmit(Kind::Go,controller->goAt)) lastGoSend=nowUs(); }
      } else if (!controller->active()) {
        // No eInk refresh here: it would age readiness observations before ARM.
        now=nowUs(); benchPeers(now);
        if (controller->arm(now,beam.clear(now))) { cancelUntil=0; lastArmSend=0; }
        else dirty=true;
      }
    }
  }
  now=nowUs();
  // Repeat the same immutable schedule; a busy/failed submission retries next loop.
  if (controller->state==State::Revealing && now<controller->revealAt &&
      (sentRevealAt!=controller->revealAt || now-lastRevealSend>=kRevealRepeatUs)) {
    if (transmit(Kind::Reveal,controller->revealAt)) {
      sentRevealAt=controller->revealAt; lastRevealSend=now;
    }
  }
  if (controller->state==State::Preparing && now-lastArmSend>=250000) {
    if (transmit(Kind::Arm)) lastArmSend=now;
  }
  if (controller->state==State::Countdown) {
    // Repeat the schedule until every required unit echoes it; tick() aborts at the first cue otherwise.
    if (!controller->cued(now) && now-lastGoSend>=250000 && transmit(Kind::Go,controller->goAt)) lastGoSend=now;
    if (!cueCount && controller->cued(now)) scheduleCues(controller->goAt);
  }
  if (controller->startPending(now) && now-lastStartSend>=100000 && transmit(Kind::Start,controller->startAt)) lastStartSend=now;
  if (controller->state!=before) {
    dirty=true;
    // Silence immediately on cancel/fault; a completed GO tone is left to finish.
    if (controller->state==State::Idle || controller->state==State::Fault) stopCues();
    if (controller->state==State::Fault) { cancelUntil=now+2000000; transmit(Kind::Cancel); beep(400,350); }
    if (Serial.availableForWrite()>80) Serial.printf("%s attempt=%lu: %s\n",stateName(controller->state),(unsigned long)controller->attempt,controller->reason);
  }
  if (now<cancelUntil && now-lastCancel>=250000) { if (transmit(Kind::Cancel)) lastCancel=now; }
  // Heartbeats continue in every state: they are the peers' clock-sync samples.
  if (now-lastHeartbeat>=500000) {
    if (transmit(Kind::Status)) lastHeartbeat=now;
  }
  if (now-lastPixels>=50000) {
    for (uint8_t i=0;i<4;++i) {
      uint32_t color=0;
      if (i<controller->lanes) switch (controller->state) {
        case State::Fault: color=pixels.Color(255,0,0); break;
        case State::Preparing: color=pixels.Color(120,40,0); break;
        case State::Armed: color=pixels.Color(0,180,0); break;
        case State::Countdown: color=(now>=controller->firstCue() && (now-controller->firstCue())%kCueStepUs<200000)?pixels.Color(180,80,0):pixels.Color(120,40,0); break;
        case State::Running: color=(controller->finished&(1<<i))?pixels.Color(0,120,0):
            (controller->started&(1<<i))?pixels.Color(0,0,180):pixels.Color(0,180,0); break;
        case State::Revealing:
        case State::Complete: color=(controller->finished&(1U<<i))?pixels.Color(0,120,0):pixels.Color(180,40,0); break;
        default: color=controller->mode==Mode::Flying && i==0 && !beam.clear(now)?pixels.Color(100,0,0):pixels.Color(20,20,20); break;
      }
      pixels.setPixelColor(i,color);
    }
    pixels.show(); lastPixels=now;
  }
  // Finish cancellation repeats before entering the blocking display driver.
  if (dirty && !controller->active() && now>=cancelUntil && now-lastPaint>=3000000) paint();
  delay(1);
}
