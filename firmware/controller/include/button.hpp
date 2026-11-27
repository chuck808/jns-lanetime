#pragma once
#include <stdint.h>
namespace jns {
// Pure button logic: a hold must begin with a fresh press while eligible.
class Button {
 public:
  // True on a debounced press. A debounced release is reported by tapped().
  bool update(bool down,uint64_t now,bool holdEligible=false) {
    released_=false;
    if (down!=raw_) { raw_=down; changed_=now; }
    if (!holdEligible) holdArmed_=false;
    if (raw_!=stable_ && now-changed_>=30000) {
      stable_=raw_;
      holdArmed_=stable_ && holdEligible; pressedAt_=now;
      if (stable_) { fired_=false; eligible_=holdEligible; return true; }
      released_=true;
    }
    return false;
  }
  bool held(uint64_t now,uint64_t duration=1000000) {
    if (!raw_ || !stable_ || !holdArmed_ || now-pressedAt_<duration) return false;
    holdArmed_=false; fired_=true; return true;
  }
  // Released on this update without completing a hold, from a press that began while
  // eligible: a short press. A press made during an attempt never becomes a tap later.
  bool tapped() const { return released_ && !fired_ && eligible_; }
 private:
  bool raw_=false, stable_=false, holdArmed_=false, released_=false, fired_=false, eligible_=false;
  uint64_t changed_=0, pressedAt_=0;
};
}
