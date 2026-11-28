#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <Preferences.h>
#include <Adafruit_NeoPixel.h>
#include <lanetime/protocol.hpp>
#include <lanetime/command.hpp>
#include "remote_logic.hpp"
using namespace jns;
// DFRobot Beetle ESP32-C6. Buttons need GPIO 0-7 to wake the C6 from deep sleep;
// GPIO 0 is the board's battery divider and 4/5 are strapping pins, hence 6 and 7.
#ifndef JNS_BTN_GO
#define JNS_BTN_GO 6
#endif
#ifndef JNS_BTN_CANCEL
#define JNS_BTN_CANCEL 7
#endif
#ifndef JNS_PIXEL
#define JNS_PIXEL 22
#endif
#ifndef JNS_PIXEL_POWER
#define JNS_PIXEL_POWER 23   // Powers the NeoPixel, so it draws nothing during sleep.
#endif
#ifndef JNS_PIXEL_RGBW
#define JNS_PIXEL_RGBW 0
#endif
#ifndef JNS_PIXEL_BRIGHTNESS
#define JNS_PIXEL_BRIGHTNESS 60
#endif
#ifndef JNS_BATTERY_PIN
#define JNS_BATTERY_PIN 0
#endif
#ifndef JNS_BATTERY_SCALE
#define JNS_BATTERY_SCALE 2   // Divider ratio on the battery pin. Check against a meter.
#endif
constexpr uint64_t kHelloUs=2000000, kIdleSleepUs=600000000, kLostSleepUs=120000000, kPairUs=35000000;
constexpr uint8_t kLowBattery=15;

Adafruit_NeoPixel pixel(1,JNS_PIXEL,(JNS_PIXEL_RGBW?NEO_GRBW:NEO_GRB)+NEO_KHZ800);
Preferences prefs;
struct Received { uint8_t mac[6]; uint8_t data[kPacketSize]; uint64_t at; };
QueueHandle_t radioQueue=nullptr;
portMUX_TYPE radioLock=portMUX_INITIALIZER_UNLOCKED;
volatile bool radioBusy=false;
const uint8_t broadcastMac[6]={0xff,0xff,0xff,0xff,0xff,0xff};
uint8_t st1[6]={}, slot=0;
bool paired=false, radioReady=false, pairing=false;
uint32_t bootId=0, seq=0;
uint8_t battery=255;
Mirror mirror;
Pending pending;
Button goButton, cancelButton;
uint64_t lastHello=0, lastActivity=0, lastPairSend=0, pairUntil=0, lastBattery=0, lowUntil=0;
enum class Feedback : uint8_t { None, Accepted, Refused, Paired };
Feedback feedback=Feedback::None;
uint64_t feedbackAt=0;
uint64_t nowUs() { return uint64_t(esp_timer_get_time()); }

void queuePacket(const uint8_t* mac,const uint8_t* data,int length) {
  if (length!=int(kPacketSize) || !radioQueue) return;
  Received r; memcpy(r.mac,mac,6); memcpy(r.data,data,kPacketSize); r.at=nowUs();
  xQueueSend(radioQueue,&r,0);  // A lost Status is replaced 500 ms later; a lost reply is resent.
}
void sendDone() { portENTER_CRITICAL(&radioLock); radioBusy=false; portEXIT_CRITICAL(&radioLock); }
#if ESP_ARDUINO_VERSION_MAJOR>=3
void received(const esp_now_recv_info_t* info,const uint8_t* data,int length) { queuePacket(info->src_addr,data,length); }
#if ESP_IDF_VERSION>=ESP_IDF_VERSION_VAL(5,5,0)
void sent(const wifi_tx_info_t*,esp_now_send_status_t) { sendDone(); }
#else
void sent(const uint8_t*,esp_now_send_status_t) { sendDone(); }
#endif
#else
#error "The remote needs Arduino-ESP32 3.x (the pioarduino platform) for the ESP32-C6."
#endif
bool addPeer(const uint8_t* mac) {
  if (esp_now_is_peer_exist(mac)) return true;
  esp_now_peer_info_t peer{}; memcpy(peer.peer_addr,mac,6);
  peer.channel=JNS_RADIO_CHANNEL; peer.ifidx=WIFI_IF_STA; peer.encrypt=false;
  return esp_now_add_peer(&peer)==ESP_OK;
}
bool send(const uint8_t* mac,Code code,uint16_t arg,uint32_t number) {
  if (!radioReady) return false;
  portENTER_CRITICAL(&radioLock);
  const bool busy=radioBusy;
  if (!busy) radioBusy=true;
  portEXIT_CRITICAL(&radioLock);
  if (busy) return false;
  Packet p;
  p.kind=Kind::Command; p.role=Role::Remote; p.lane=slot?slot:1; p.system=JNS_SYSTEM_ID;
  p.session=mirror.session; p.attempt=mirror.attempt; p.mode=mirror.mode; p.mask=mirror.mask?mirror.mask:1;
  p.boot=bootId;
  Command c; c.code=code; c.arg=arg; c.seq=number; p.value=packCommand(c);
  uint8_t bytes[kPacketSize]; encode(p,bytes);
  if (esp_now_send(mac,bytes,sizeof(bytes))!=ESP_OK) { sendDone(); return false; }
  return true;
}
void startRadio() {
  WiFi.mode(WIFI_STA); WiFi.disconnect();
  radioQueue=xQueueCreate(16,sizeof(Received));
  radioReady=radioQueue && esp_wifi_set_channel(JNS_RADIO_CHANNEL,WIFI_SECOND_CHAN_NONE)==ESP_OK &&
    esp_now_init()==ESP_OK && esp_now_register_recv_cb(received)==ESP_OK &&
    esp_now_register_send_cb(sent)==ESP_OK && addPeer(broadcastMac);
  if (radioReady && paired) addPeer(st1);
}
void loadPairing() {
  prefs.begin("jns-remote",false);
  paired=prefs.getBytes("st1",st1,6)==6 && prefs.getUChar("slot",0)>=1 && prefs.getUChar("slot",0)<=4;
  slot=paired?prefs.getUChar("slot",0):0;
}
void savePairing(const uint8_t* mac,uint8_t assigned) {
  memcpy(st1,mac,6); slot=assigned; paired=true;
  prefs.putBytes("st1",st1,6); prefs.putUChar("slot",slot);
  addPeer(st1);
}
void readBattery(uint64_t now) {
  const uint32_t mv=analogReadMilliVolts(JNS_BATTERY_PIN)*JNS_BATTERY_SCALE;
  battery=mv<1000?255:batteryPercent(mv);  // Under 1 V: no battery reading (USB only).
  lastBattery=now;
}
void handlePacket(const Received& r) {
  Packet p;
  if (!decode(r.data,sizeof(r.data),p) || p.system!=JNS_SYSTEM_ID || p.role!=Role::Controller) return;
  const bool fromSt1=paired && !memcmp(r.mac,st1,6);
  if (p.kind==Kind::Status) { if (fromSt1) mirror.take(p,r.at); return; }
  if (p.kind!=Kind::Command) return;
  Command c;
  if (!unpackCommand(p.value,c)) return;
  if (pairing && c.code==Code::Pair) {
    if (c.reply==Reply::Accepted && c.arg>=1 && c.arg<=4) {
      savePairing(r.mac,uint8_t(c.arg)); pairing=false; feedback=Feedback::Paired; feedbackAt=nowUs();
    } else if (c.reply==Reply::Refused) { pairing=false; feedback=Feedback::Refused; feedbackAt=nowUs(); }
    return;
  }
  if (!fromSt1 || !pending.answer(c)) return;
  feedback=c.reply==Reply::Accepted?Feedback::Accepted:Feedback::Refused; feedbackAt=nowUs();
  if (c.reply==Reply::Refused) Serial.printf("Refused: %s\n",reasonName(c.reason));
}
void startCommand(Code code,uint64_t now) {
  if (!paired) { feedback=Feedback::Refused; feedbackAt=now; return; }
  seq=(seq+1)&kSeqMask; if (!seq) seq=1;
  pending.start(code,seq,now);
}
void showLight(uint64_t now) {
  static uint32_t shown=0xFFFFFFFF;
  Glow g=pairing?Glow::Pairing:glowFor(mirror,slot,now);
  if (!paired && !pairing) g=Glow::Searching;
  Rgb c=colourFor(g,now);
  // Low battery: red flashing for 3 s after waking or switching on.
  if (now<lowUntil) c=(now/150000)%2?Rgb{240,30,20}:Rgb{0,0,0};
  // Press feedback: a bright flash when accepted, a double red blink when refused or lost.
  const uint64_t since=now-feedbackAt;
  if (feedback==Feedback::Accepted) { if (since<150000) c=Rgb{255,255,255}; else feedback=Feedback::None; }
  else if (feedback==Feedback::Refused) { if (since<480000) c=(since/120000)%2?Rgb{0,0,0}:Rgb{255,20,10}; else feedback=Feedback::None; }
  else if (feedback==Feedback::Paired) { if (since<900000) c=(since/150000)%2?Rgb{0,0,0}:Rgb{60,255,60}; else feedback=Feedback::None; }
  const uint8_t k=JNS_PIXEL_BRIGHTNESS;
  const uint32_t colour=pixel.Color(uint8_t(c.r*k/255),uint8_t(c.g*k/255),uint8_t(c.b*k/255));
  if (colour==shown) return;
  shown=colour; pixel.setPixelColor(0,colour); pixel.show();
}
void sleepNow() {
  pixel.clear(); pixel.show(); digitalWrite(JNS_PIXEL_POWER,LOW);
  if (radioReady) esp_now_deinit();
  WiFi.mode(WIFI_OFF);
  const gpio_num_t pins[]={gpio_num_t(JNS_BTN_GO),gpio_num_t(JNS_BTN_CANCEL)};
  for (gpio_num_t pin:pins) { rtc_gpio_init(pin); rtc_gpio_set_direction(pin,RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(pin); rtc_gpio_pulldown_dis(pin); }
  esp_sleep_enable_ext1_wakeup_io((1ULL<<JNS_BTN_GO)|(1ULL<<JNS_BTN_CANCEL),ESP_EXT1_WAKEUP_ANY_LOW);
  esp_deep_sleep_start();
}
void setup() {
  Serial.begin(115200);
  const bool woke=esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_EXT1;
  rtc_gpio_deinit(gpio_num_t(JNS_BTN_GO)); rtc_gpio_deinit(gpio_num_t(JNS_BTN_CANCEL));
  pinMode(JNS_BTN_GO,INPUT_PULLUP); pinMode(JNS_BTN_CANCEL,INPUT_PULLUP);
  pinMode(JNS_PIXEL_POWER,OUTPUT); digitalWrite(JNS_PIXEL_POWER,HIGH); delay(2);
  pixel.begin(); pixel.clear(); pixel.show();
  bootId=esp_random(); if (!bootId) bootId=1;
  loadPairing();
  // Both buttons held for 2 s while switching on: pair with an ST-1 whose pairing window is open.
  if (!woke && digitalRead(JNS_BTN_GO)==LOW && digitalRead(JNS_BTN_CANCEL)==LOW) {
    const uint64_t t=nowUs();
    while (digitalRead(JNS_BTN_GO)==LOW && digitalRead(JNS_BTN_CANCEL)==LOW && nowUs()-t<2000000) {
      pixel.setPixelColor(0,pixel.Color(30,30,30)); pixel.show(); delay(20);
    }
    if (nowUs()-t>=2000000) { pairing=true; pairUntil=nowUs()+kPairUs; }
  }
  // The press that woke it (or the pairing hold) only wakes it: it never acts.
  if (digitalRead(JNS_BTN_GO)==LOW) goButton.block();
  if (digitalRead(JNS_BTN_CANCEL)==LOW) cancelButton.block();
  startRadio();
  const uint64_t now=nowUs();
  readBattery(now);
  if (battery<=kLowBattery) lowUntil=now+3000000;
  lastActivity=now;
  Serial.printf("JNS remote boot=%08lX paired=%d slot=%u battery=%u%%\n",(unsigned long)bootId,paired,slot,battery);
}
void loop() {
  uint64_t now=nowUs();
  Received r;
  while (radioQueue && xQueueReceive(radioQueue,&r,0)==pdTRUE) handlePacket(r);
  if (now-lastBattery>=10000000) readBattery(now);
  const bool goPressed=goButton.update(digitalRead(JNS_BTN_GO)==LOW,now);
  const bool cancelPressed=cancelButton.update(digitalRead(JNS_BTN_CANCEL)==LOW,now);
  if (goPressed || cancelPressed || goButton.down() || cancelButton.down()) lastActivity=now;
  if (!pairing) {
    if (cancelPressed) startCommand(Code::Cancel,now);  // CANCEL wins over a simultaneous GO.
    else if (goPressed) {
      const uint8_t code=pressCode(mirror,slot,now);
      if (code) startCommand(Code(code),now);
      else { feedback=Feedback::Refused; feedbackAt=now; }  // The press did nothing: say so.
    } else if (goButton.down()) {
      const uint8_t code=holdCode(mirror,slot,now,goButton.heldFor(now));
      if (code && goButton.fireOnce()) startCommand(Code(code),now);
    }
  }
  if (pending.due(now)) send(st1,pending.code(),pending.ageMs(now),pending.seq());
  if (pending.takeExpired()) { feedback=Feedback::Refused; feedbackAt=now; }
  if (paired && !pairing && now-lastHello>=kHelloUs) { if (send(st1,Code::Hello,battery,0)) lastHello=now; }
  if (pairing) {
    if (now>=pairUntil) pairing=false;
    else if (now-lastPairSend>=500000 && send(broadcastMac,Code::Pair,0,0)) lastPairSend=now;
  }
  showLight(now);
  // Sleep: 10 min without a press and no attempt in progress, or 2 min without hearing ST-1.
  const bool attempt=mirror.fresh(now) && (mirror.flags&(Armed|Running));
  const bool lost=!mirror.fresh(now);
  const uint64_t idle=now-lastActivity;
  if (!pairing && !pending.active() && !goButton.down() && !cancelButton.down() &&
      digitalRead(JNS_BTN_GO)==HIGH && digitalRead(JNS_BTN_CANCEL)==HIGH &&
      ((!attempt && idle>=kIdleSleepUs) || (lost && idle>=kLostSleepUs))) sleepNow();
  delay(1);
}
