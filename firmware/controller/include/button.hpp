#pragma once
#include <stdint.h>
namespace jns {
// Pure button logic: a hold must begin with a fresh press while eligible.
class Button {
 public:
  bool update(bool down,uint64_t now,bool holdEligible=false) {
    if (down!=raw_) { raw_=down; changed_=now; }
    if (!holdEligible) holdArmed_=false;
    if (raw_!=stable_ && now-changed_>=30000) {
      stable_=raw_;
      holdArmed_=stable_ && holdEligible; pressedAt_=now;
      return stable_;
    }
    return false;
  }
  bool held(uint64_t now) {
    if (!raw_ || !stable_ || !holdArmed_ || now-pressedAt_<1000000) return false;
    holdArmed_=false; return true;
  }
 private:
  bool raw_=false, stable_=false, holdArmed_=false;
  uint64_t changed_=0, pressedAt_=0;
};
}
