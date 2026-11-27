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
#include "efforts.hpp"
#include "upload.hpp"
#include <lanetime/cue_player.hpp>
#include <freertos/semphr.h>
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
// Preset distances for the MODE hold. JNS SPLIT takes the distance from its own session,
// so set the session to match: this one is for the coach's screen and the speed figure.
constexpr uint16_t kDistances[]={10,20,30,50};
uint16_t distance=kDistances[0];
EffortQueue efforts;
char notice[64]="";
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
  p.distance=uint8_t(distance);  // Lets lane displays show speed.
  if (kind==Kind::Status && controller->state==State::Idle && controller->attempt) p.flags|=jns::Retired;
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
SemaphoreHandle_t cueMutex=nullptr;
esp_timer_handle_t cueTimer=nullptr;
CuePlayer cuePlayer;
uint64_t scheduledGo=0, cueLease=0;
uint16_t alertHz=0;
void cueFire(void*) {
  if (xSemaphoreTake(cueMutex,0)!=pdTRUE) return;
  const uint64_t now=nowUs();
  uint16_t hz=cuePlayer.tick(now,cueLease);
  if (toneUntil && now<toneUntil) hz=alertHz;
  else toneUntil=0;
  static uint16_t previous=0;
  if (hz!=previous) {
    if (hz) { digitalWrite(speakerEnable,HIGH); ledcWriteTone(soundChannel,hz); }
    else { ledcWrite(soundChannel,0); digitalWrite(speakerEnable,LOW); }
    previous=hz;
  }
  xSemaphoreGive(cueMutex);
}
void stopCues() {
  if (!cueMutex) return;
  xSemaphoreTake(cueMutex,portMAX_DELAY);
  cuePlayer.stop(); scheduledGo=0; toneUntil=0;
  xSemaphoreGive(cueMutex);
}
void scheduleCues(uint64_t goAt) {
  if (!cueMutex) return;
  xSemaphoreTake(cueMutex,portMAX_DELAY);
  const uint64_t now=nowUs();
  if (goAt<3000000 || goAt-3000000<=now+20000) controller->fail("Cue schedule too late");
  else {
    uint64_t times[8];
    for (uint8_t i=0;i<8;++i) {
      const uint8_t tone=i/2;
      times[i]=goAt-(3-tone)*1000000ULL+(i%2?(tone==3?400000:120000):0);
    }
    cuePlayer.schedule(times); scheduledGo=goAt; cueLease=now+100000;
  }
  xSemaphoreGive(cueMutex);
}
void serviceCues() {
  if (!cueMutex) return;
  xSemaphoreTake(cueMutex,portMAX_DELAY);
  if (cuePlayer.failed()) { controller->fail("Cue timer missed deadline"); fatalHardware=true; }
  cueLease=nowUs()+100000;
  xSemaphoreGive(cueMutex);
}
void beep(uint16_t hz,uint32_t durationMs) {
  if (!cueMutex) return;
  xSemaphoreTake(cueMutex,portMAX_DELAY);
  alertHz=hz; toneUntil=nowUs()+uint64_t(durationMs)*1000;
  xSemaphoreGive(cueMutex);
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
  display.printf("%s | Lanes 1-%u | %u m | Attempt %lu",controller->mode==Mode::Flying?"FLYING":"STANDING",
    controller->lanes,distance,(unsigned long)controller->attempt);
  display.setCursor(0,36); display.print(stateName(controller->state));
  display.setCursor(0,46); display.print(controller->reason);
  display.setCursor(0,56); display.printf("Lane 1 beam: %s   To upload: %u",beam.clear(nowUs())?"CLEAR":"BROKEN",efforts.pending());
  if (controller->state==State::Complete) {
    for (uint8_t i=0;i<controller->lanes;++i) {
      display.setCursor(0,68+10*i);
      if (controller->finished&(1U<<i)) {
        const uint32_t ms=wholeMs(controller->results[i]), kmh=tenthsKmh(distance,ms);
        display.printf("L%u  %lu.%03lu s   %lu.%lu km/h",i+1,(unsigned long)(ms/1000),(unsigned long)(ms%1000),
          (unsigned long)(kmh/10),(unsigned long)(kmh%10));
      } else display.printf("L%u  DNF",i+1);
    }
  } else {
    display.setCursor(0,68); display.print("LIVE LEDs: green=armed blue=run red=invalid");
    display.setCursor(0,78); display.print("Prototype - timing accuracy unverified");
  }
  display.setCursor(0,112); display.print("MODE/hold:dist LANES CANCEL/hold:upload GO");
  display.display(); dirty=false; lastPaint=nowUs();
}
void saveSettings() {
  preferences.putUChar("mode",uint8_t(controller->mode)); preferences.putUChar("lanes",controller->lanes);
  preferences.putUShort("dist",distance);
}
Button button[4];
void startRadio() {
  esp_now_peer_info_t peer{}; memcpy(peer.peer_addr,broadcastMac,6);
  peer.channel=JNS_RADIO_CHANNEL; peer.ifidx=WIFI_IF_STA; peer.encrypt=false;
  radioReady=esp_wifi_set_channel(JNS_RADIO_CHANNEL,WIFI_SECOND_CHAN_NONE)==ESP_OK &&
    esp_now_init()==ESP_OK && esp_now_register_recv_cb(received)==ESP_OK &&
    esp_now_register_send_cb(sent)==ESP_OK && esp_now_add_peer(&peer)==ESP_OK;
  if (!radioReady) { controller->fail("Radio setup failed"); fatalHardware=true; }
}
// Queue each attempt that riders started, as it ends. Room is checked before arming.
void takeClosed() {
  if (!controller->closed.pending) return;
  controller->closed.pending=false;
  if (!efforts.add(controller->closed,bootId,distance)) {
    if (Serial.availableForWrite()>80) Serial.println("EFFORTS not queued: queue full");
    return;
  }
  uint8_t added=0;
  for (uint8_t lane=0;lane<4;++lane) if (controller->closed.mask&(1U<<lane)) ++added;
  for (uint8_t i=efforts.size()-added;i<efforts.size();++i) {
    const Effort& e=efforts.at(i);
    if (Serial.availableForWrite()>80) Serial.printf("EFFORT %s lane=%u %s %lu ms %u m\n",
      e.id,e.lane,outcomeName(e.outcome),(unsigned long)e.ms,e.distance);
  }
}
// The JNS Pro pattern: timing radio off, Wi-Fi on to upload, then timing radio back on.
// Sensors lose ST-1's heartbeat meanwhile and need about 8 s to be Ready again.
void uploadSession() {
  efforts.compact();
  if (!efforts.pending()) {
    snprintf(notice,sizeof(notice),"Nothing to upload"); controller->reason=notice; dirty=true; return;
  }
  snprintf(notice,sizeof(notice),"Uploading %u - timing radio paused",efforts.pending());
  controller->reason=notice; paint();
  if (!JNS_BENCH) { esp_now_deinit(); radioReady=false; }
  const UploadSummary s=uploadPending(efforts);
  if (!JNS_BENCH) startRadio();
  lastHeartbeat=0;
  if (!s.configured || !s.connected) snprintf(notice,sizeof(notice),"Upload failed: %s",s.stopCode);
  else if (s.stopCode[0]) snprintf(notice,sizeof(notice),"Saved %u, refused %u, %u left: %s",s.saved,s.rejected,s.left,s.stopCode);
  else snprintf(notice,sizeof(notice),"Saved %u, refused %u, %u left",s.saved,s.rejected,s.left);
  controller->reason=notice;
  if (Serial.availableForWrite()>80) Serial.printf("UPLOAD done: %s\n",notice);
  efforts.compact(); dirty=true; lastPaint=0;
}
void benchPeers(uint64_t now) {
  if (!JNS_BENCH) return;
  for (uint8_t lane=1;lane<=controller->lanes;++lane) for (Role role:{Role::Start,Role::Finish}) {
    if (role==Role::Start && lane==1) continue;
    auto p=packet(Kind::Status); p.role=role; p.lane=lane; p.boot=100+lane+10*uint8_t(role);
    p.flags=jns::Ready | (controller->active()?jns::Armed:0);
    if (controller->goAt) { p.flags|=jns::Cued; p.value=controller->goAt; }
    uint8_t mac[]={2,0,0,0,uint8_t(role),lane}; controller->observe(mac,p,now);
  }
  // Simulated finishes a few seconds after each started lane, so a bench run produces
  // completed efforts to upload. Flying lane 1 starts from the real local beam.
  if (controller->state!=State::Running) return;
  for (uint8_t lane=1;lane<=controller->lanes;++lane) {
    const uint8_t bit=uint8_t(1U<<(lane-1));
    if (!(controller->started&bit) || (controller->finished&bit) || (controller->mode==Mode::Flying && lane>1)) continue;
    const uint64_t from=controller->mode==Mode::Flying?controller->startAt:controller->goAt;
    const uint64_t elapsed=2400000+uint64_t(lane)*173000+uint64_t(controller->attempt%7)*31000;
    if (now<from+elapsed) continue;
    auto p=packet(Kind::Result); p.role=Role::Finish; p.lane=lane; p.boot=100+lane+10*uint8_t(Role::Finish);
    p.flags=jns::Complete; p.value=elapsed;
    uint8_t mac[]={2,0,0,0,uint8_t(Role::Finish),lane}; controller->observe(mac,p,now);
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
  distance=preferences.getUShort("dist",kDistances[0]);
  bool known=false;
  for (auto d:kDistances) known|=d==distance;
  if (!known) distance=kDistances[0];
  if (!radioQueue || !edgeQueue) { controller->fail("Queue allocation failed"); fatalHardware=true; }
  if (!JNS_BENCH && !fatalHardware) startRadio();
  const double irHz=ledcSetup(irChannel,38000,8);
  ledcAttachPin(txPin,irChannel); ledcWrite(irChannel,0);
  const double soundHz=ledcSetup(soundChannel,2000,8);
  ledcAttachPin(speakerPin,soundChannel); ledcWrite(soundChannel,0);
  const esp_timer_create_args_t cueArgs={cueFire,nullptr,ESP_TIMER_TASK,"cues",false};
  cueMutex=xSemaphoreCreateMutex();
  if (!cueMutex || soundHz<1900 || soundHz>2100 || esp_timer_create(&cueArgs,&cueTimer)!=ESP_OK ||
      esp_timer_start_periodic(cueTimer,1000)!=ESP_OK) { controller->fail("Cue timer setup failed"); fatalHardware=true; }
  if (irHz<37500 || irHz>38500) { controller->fail("IR PWM setup failed"); fatalHardware=true; }
  SPI.begin(36,37,35,8); display.begin(THINKINK_MONO); display.setRotation(0);
  beam.edge(digitalRead(rxPin)!=LOW,nowUs());
  if (edgeQueue) attachInterrupt(digitalPinToInterrupt(rxPin),beamEdge,CHANGE);
  if (!fatalHardware) ledcWrite(irChannel,128);
  paint();
}
void loop() {
  uint64_t now=nowUs(); const State before=controller->state;
  serviceCues();
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
  const bool idle=!controller->active() && !fatalHardware;
  for (int i=0;i<4;++i) pressed[i]=button[i].update(digitalRead(buttons[i])==LOW,now,
      i==3 ? before==State::Running && controller->state==State::Running : (i==0 || i==2) && idle);
  const bool revealHeld=button[3].held(now);
  // MODE: tap switches Flying/Standing; hold cycles the distance. CANCEL: hold uploads.
  const bool modeTap=button[0].tapped(), distanceHeld=button[0].held(now);
  const bool uploadHeld=button[2].held(now,2000000);
  // CANCEL wins over simultaneous GO.
  if (pressed[2] && !fatalHardware) {
    controller->cancel(); cancelUntil=now+2000000; transmit(Kind::Cancel); dirty=true;
  } else if (!fatalHardware) {
    if (revealHeld) controller->reveal(now);
    if (!controller->active() && (modeTap || distanceHeld || pressed[1])) {
      const bool retire=controller->state!=State::Idle;
      controller->cancel();
      if (retire) { cancelUntil=now+2000000; transmit(Kind::Cancel); }
      if (modeTap) controller->mode=controller->mode==Mode::Flying?Mode::Standing:Mode::Flying;
      if (distanceHeld) {
        uint8_t next=0;
        for (uint8_t i=0;i<sizeof(kDistances)/sizeof(kDistances[0]);++i) if (kDistances[i]==distance) next=i+1;
        distance=kDistances[next%(sizeof(kDistances)/sizeof(kDistances[0]))];
      }
      if (pressed[1]) controller->lanes=controller->lanes%4+1;
      saveSettings(); dirty=true;
    }
    if (uploadHeld && !controller->active() && now>=cancelUntil) uploadSession();
    if (pressed[3]) {
      if (controller->state==State::Armed && controller->mode==Mode::Standing) {
        if (controller->countdown(now)) { stopCues(); lastGoSend=0; if (transmit(Kind::Go,controller->goAt)) lastGoSend=nowUs(); }
      } else if (!controller->active()) {
        // Never start an attempt whose efforts could not be kept.
        if (efforts.room()<controller->lanes) {
          snprintf(notice,sizeof(notice),"Upload queue full - hold CANCEL to upload");
          controller->reason=notice; dirty=true;
        } else {
          // No eInk refresh here: it would age readiness observations before ARM.
          now=nowUs(); benchPeers(now);
          if (controller->arm(now,beam.clear(now))) { cancelUntil=0; lastArmSend=0; }
          else dirty=true;
        }
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
    if (!scheduledGo && controller->cued(now)) scheduleCues(controller->goAt);
  }
  takeClosed();
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
