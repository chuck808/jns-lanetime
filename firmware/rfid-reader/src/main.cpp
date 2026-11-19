#include <M5Unified.h>
#include <MFRC522_I2C.h>
#include <Wire.h>
#include <esp_system.h>
#include "presentation.hpp"
#ifndef JNS_READER_LANE
#define JNS_READER_LANE 1
#endif
static_assert(JNS_READER_LANE >= 1 && JNS_READER_LANE <= 4, "Lane must be 1..4");
// Core2 Port A is SDA32/SCL33. Wire is the external bus; M5Unified owns internal I2C.
MFRC522_I2C rfid(0x28, -1, &Wire);
Presentation presentation;
char readerId[32], bootId[25];
uint32_t sequence = 0, lastPoll = 0;
bool readerReady = false, missingShown = false;
void screen(const char* title, const char* detail) {
  M5.Display.fillScreen(0x1082);
  M5.Display.setTextColor(0xCFE8,0x1082);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(12,16);
  M5.Display.println("JNS SPLIT / BENCH");
  M5.Display.setTextColor(0xFFFF,0x1082);
  M5.Display.setCursor(12,55); M5.Display.println(title);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(12,95); M5.Display.println(detail);
  M5.Display.setCursor(12,155); M5.Display.println("USB scan output only. No lane approval.");
  M5.Display.setCursor(12,180); M5.Display.printf("Lane %d | Remove tag before next tap", JNS_READER_LANE);
}
bool probe() { Wire.beginTransmission(0x28); return Wire.endTransmission()==0; }
void setup() {
  auto cfg=M5.config(); cfg.internal_imu=false; cfg.internal_rtc=false;
  M5.begin(cfg); Serial.begin(115200);
  M5.Display.setRotation(1); M5.Display.setBrightness(100);
  Wire.begin(32,33,100000); Wire.setTimeOut(50);
  const uint64_t mac=ESP.getEfuseMac();
  snprintf(readerId,sizeof(readerId),"core2-%04X%08X",unsigned(mac>>32),unsigned(mac));
  snprintf(bootId,sizeof(bootId),"%08X%08X%08X",unsigned(esp_random()),unsigned(esp_random()),unsigned(esp_random()));
  screen("Starting reader...","RFID2 on Port A / I2C 0x28");
}
void loop() {
  M5.update(); const uint32_t now=millis();
  if(uint32_t(now-lastPoll)<100) return;
  lastPoll=now;
  if(!probe()) {
    presentation.uncertain();
    if (!missingShown) screen("RFID2 not found", "Check Port A cable; reconnect to retry.");
    missingShown = true;
    readerReady=false; return;
  }
  if(!readerReady) { missingShown=false;rfid.PCD_Init();readerReady=true;screen("Waiting for tag","Present an NTAG or MIFARE Type A tag."); }
  // Wake halted tags too, so a held tag remains present after PICC_HaltA.
  byte atqa[2]; byte size=sizeof(atqa);
  const byte status=rfid.PICC_WakeupA(atqa,&size);
  if(status==MFRC522_I2C::STATUS_TIMEOUT) { presentation.absent(now);return; }
  if(status!=MFRC522_I2C::STATUS_OK) { presentation.uncertain();return; }
  if(!rfid.PICC_ReadCardSerial()) { presentation.uncertain();return; }
  if(presentation.observe(rfid.uid.uidByte,rfid.uid.size,now)) {
    char uid[21]{};
    for(size_t i=0;i<presentation.size();++i) snprintf(uid+2*i,3,"%02X",presentation.uid()[i]);
    ++sequence;
    Serial.printf("{\"version\":1,\"type\":\"scan\",\"readerId\":\"%s\",\"requestId\":\"%s-%lu\",\"lane\":%d,\"uid\":\"%s\"}\n",readerId,bootId,(unsigned long)sequence,JNS_READER_LANE,uid);
    screen("Tag read - not checked in",uid);
  }
  rfid.PICC_HaltA(); rfid.PCD_StopCrypto1();
}
