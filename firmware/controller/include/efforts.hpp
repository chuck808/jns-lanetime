#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "controller.hpp"
#include <lanetime/speed.hpp>
namespace jns {
// One lane of one attempt, as JNS SPLIT results upload v1 expects it.
enum class Outcome : uint8_t { Completed, Dnf, Cancelled, Invalid };
enum class Upload : uint8_t { Pending, Saved, Rejected };
inline const char* outcomeName(Outcome o) {
  switch (o) {
    case Outcome::Completed: return "completed";
    case Outcome::Dnf: return "dnf";
    case Outcome::Cancelled: return "cancelled";
    case Outcome::Invalid: return "invalid";
  }
  return "invalid";
}
struct Effort {
  char id[24]={};
  uint8_t lane=1;
  Outcome outcome=Outcome::Invalid;
  uint32_t ms=0;        // Whole milliseconds; only for Completed.
  uint16_t distance=0;  // Metres set on ST-1 when the attempt ran.
  Upload upload=Upload::Pending;
  char code[28]={};     // Last reply code from JNS SPLIT, for the screen and serial log.
};
class EffortQueue {
 public:
  enum : uint8_t { kCapacity=64 };
  uint8_t size() const { return size_; }
  uint8_t room() const { return uint8_t(kCapacity-size_); }
  uint8_t pending() const {
    uint8_t n=0;
    for (uint8_t i=0;i<size_;++i) if (items_[i].upload==Upload::Pending) ++n;
    return n;
  }
  Effort& at(uint8_t i) { return items_[i]; }
  const Effort& at(uint8_t i) const { return items_[i]; }
  // Every enabled lane of the attempt, or nothing at all if there is not room for them all.
  // The result ID is the same on every retry: boot ID, attempt number and lane.
  bool add(const Closed& c,uint32_t boot,uint16_t distance) {
    uint8_t lanes=0;
    for (uint8_t lane=1;lane<=4;++lane) if (c.mask&(1U<<(lane-1))) ++lanes;
    if (!lanes || lanes>room()) return false;
    for (uint8_t lane=1;lane<=4;++lane) {
      const uint8_t bit=uint8_t(1U<<(lane-1));
      if (!(c.mask&bit)) continue;
      Effort& e=items_[size_++];
      e=Effort();
      snprintf(e.id,sizeof(e.id),"%08lX-%lu-L%u",(unsigned long)boot,(unsigned long)c.attempt,lane);
      e.lane=lane; e.distance=distance;
      if (c.ending==Ending::Cancelled) e.outcome=Outcome::Cancelled;
      else if (c.ending==Ending::Invalid) e.outcome=Outcome::Invalid;
      else if (c.finished&bit) { e.outcome=Outcome::Completed; e.ms=wholeMs(c.results[lane-1]); }
      else e.outcome=Outcome::Dnf;
    }
    return true;
  }
  // Forget efforts that JNS SPLIT has saved or refused; keep everything still to send.
  void compact() {
    uint8_t kept=0;
    for (uint8_t i=0;i<size_;++i) if (items_[i].upload==Upload::Pending) items_[kept++]=items_[i];
    size_=kept;
  }
 private:
  Effort items_[kCapacity];
  uint8_t size_=0;
};
// Request body for POST /api/timing/efforts. Returns the length, or 0 if it did not fit.
inline size_t effortJson(const Effort& e,const char* session,const char* uid,char* out,size_t n) {
  int len;
  if (e.outcome==Outcome::Completed)
    len=snprintf(out,n,"{\"version\":1,\"resultId\":\"%s\",\"sessionId\":\"%s\",\"lane\":%u,"
      "\"uid\":\"%s\",\"outcome\":\"completed\",\"elapsedMs\":%lu}",
      e.id,session,e.lane,uid,(unsigned long)e.ms);
  else
    len=snprintf(out,n,"{\"version\":1,\"resultId\":\"%s\",\"sessionId\":\"%s\",\"lane\":%u,"
      "\"uid\":\"%s\",\"outcome\":\"%s\"}",e.id,session,e.lane,uid,outcomeName(e.outcome));
  return len>0 && size_t(len)<n?size_t(len):0;
}
// The "code" field of a JNS SPLIT reply, e.g. SAVED or TAG_UNAVAILABLE.
inline bool replyCode(const char* body,char* code,size_t n) {
  if (!body || !n) return false;
  const char* p=strstr(body,"\"code\"");
  if (!p) return false;
  p+=6;
  while (*p==' ' || *p==':') ++p;
  if (*p!='"') return false;
  ++p;
  size_t i=0;
  while (p[i] && p[i]!='"' && i+1<n) { code[i]=p[i]; ++i; }
  if (p[i]!='"') return false;
  code[i]=0; return i>0;
}
// What one reply means for the effort, and whether to stop sending the rest.
// 200: saved (a replay is saved too). 422: JNS SPLIT refuses this effort, so drop it.
// RESULT_ID_CONFLICT is a firmware bug: drop it rather than retry for ever.
// Anything else (no reply, 400, 401, 503, limit reached) keeps it and stops the round.
inline Upload judge(int http,const char* code,bool& stop) {
  stop=false;
  if (http==200 && code && !strcmp(code,"SAVED")) return Upload::Saved;
  if (http==422) return Upload::Rejected;
  if (http==409 && code && !strcmp(code,"RESULT_ID_CONFLICT")) return Upload::Rejected;
  stop=true; return Upload::Pending;
}
}
