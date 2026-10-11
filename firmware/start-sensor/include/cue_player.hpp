#pragma once
#include <stdint.h>
namespace jns {
// Called by the timer task, under the adapter's mutex. No late cue replay.
class CuePlayer {
 public:
  void stop() { active_=false; hz_=0; }
  void schedule(const uint64_t* times) {
    for (uint8_t i=0;i<8;++i) times_[i]=times[i];
    next_=0; hz_=0; active_=true; failed_=false;
  }
  uint16_t tick(uint64_t now,uint64_t lease) {
    if (!active_) return 0;
    if (now>lease || (next_<8 && now>times_[next_]+20000)) {
      stop(); failed_=true; return 0;
    }
    if (next_<8 && now>=times_[next_]) {
      hz_=next_%2?0:(next_==6?2000:1000);
      if (++next_==8) active_=false;
    }
    return hz_;
  }
  bool failed() const { return failed_; }
 private:
  uint64_t times_[8]={};
  uint8_t next_=0;
  uint16_t hz_=0;
  bool active_=false,failed_=false;
};
}
