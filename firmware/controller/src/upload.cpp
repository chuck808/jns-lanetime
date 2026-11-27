#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "upload.hpp"
#if __has_include("upload_config.h")
#include "upload_config.h"
#endif
#ifndef JNS_WIFI_SSID
#define JNS_WIFI_SSID ""
#endif
#ifndef JNS_WIFI_PASSWORD
#define JNS_WIFI_PASSWORD ""
#endif
#ifndef JNS_UPLOAD_URL
#define JNS_UPLOAD_URL ""
#endif
#ifndef JNS_UPLOAD_CREDENTIAL
#define JNS_UPLOAD_CREDENTIAL ""
#endif
#ifndef JNS_UPLOAD_SESSION
#define JNS_UPLOAD_SESSION ""
#endif
#ifndef JNS_UPLOAD_TAG
#define JNS_UPLOAD_TAG ""
#endif
#ifndef JNS_UPLOAD_ROOT_CA
#define JNS_UPLOAD_ROOT_CA nullptr
#endif
namespace jns {
namespace {
constexpr uint32_t kJoinMs=20000;
constexpr uint16_t kReplyMs=10000;
void note(UploadSummary& s,const char* code) {
  strncpy(s.stopCode,code,sizeof(s.stopCode)-1); s.stopCode[sizeof(s.stopCode)-1]=0;
}
}
UploadSummary uploadPending(EffortQueue& queue) {
  UploadSummary s;
  const char* url=JNS_UPLOAD_URL;
  const bool https=!strncmp(url,"https://",8);
  s.configured=*JNS_WIFI_SSID && *JNS_UPLOAD_CREDENTIAL && *JNS_UPLOAD_SESSION && *JNS_UPLOAD_TAG &&
    (https || !strncmp(url,"http://",7));
  s.left=queue.pending();
  if (!s.configured) { note(s,"NOT_CONFIGURED"); return s; }
  const char* rootCa=JNS_UPLOAD_ROOT_CA;
  if (https && !rootCa) { note(s,"NO_ROOT_CERTIFICATE"); return s; }
  WiFi.persistent(false);  // Never store the network password in flash.
  WiFi.mode(WIFI_STA);
  WiFi.begin(JNS_WIFI_SSID,JNS_WIFI_PASSWORD);
  const uint32_t started=millis();
  while (WiFi.status()!=WL_CONNECTED && millis()-started<kJoinMs) delay(100);
  if (WiFi.status()!=WL_CONNECTED) {
    note(s,"WIFI_UNAVAILABLE"); WiFi.disconnect(); return s;
  }
  s.connected=true;
  WiFiClient plain;
  WiFiClientSecure secure;
  if (https) secure.setCACert(rootCa);
  const String auth=String("Bearer ")+JNS_UPLOAD_CREDENTIAL;
  for (uint8_t i=0;i<queue.size();++i) {
    Effort& e=queue.at(i);
    if (e.upload!=Upload::Pending) continue;
    char body[384];
    const size_t length=effortJson(e,JNS_UPLOAD_SESSION,JNS_UPLOAD_TAG,body,sizeof(body));
    if (!length) { note(s,"BODY_TOO_LONG"); break; }
    HTTPClient http;
    http.setConnectTimeout(kReplyMs); http.setTimeout(kReplyMs);
    const bool begun=https?http.begin(secure,url):http.begin(plain,url);
    if (!begun) { note(s,"BAD_URL"); break; }
    http.addHeader("Content-Type","application/json");
    http.addHeader("Authorization",auth);
    const int status=http.POST(reinterpret_cast<uint8_t*>(body),length);
    char code[sizeof(e.code)]={};
    if (status>0) {
      const String reply=http.getString();
      if (!replyCode(reply.c_str(),code,sizeof(code))) snprintf(code,sizeof(code),"HTTP_%d",status);
    } else snprintf(code,sizeof(code),"NO_REPLY");
    http.end();
    memcpy(e.code,code,sizeof(e.code));
    bool stop=false;
    e.upload=judge(status,code,stop);
    Serial.printf("UPLOAD %s lane=%u %s -> %d %s\n",e.id,e.lane,outcomeName(e.outcome),status,code);
    if (e.upload==Upload::Saved) ++s.saved;
    else if (e.upload==Upload::Rejected) ++s.rejected;
    if (stop) { note(s,code); break; }
  }
  s.left=queue.pending();
  WiFi.disconnect();
  delay(200);  // Let the station leave before ESP-NOW retunes to its fixed channel.
  return s;
}
}
