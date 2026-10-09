#pragma once
#include <stdint.h>
namespace jns {
// Provisional qualification thresholds: measure on the actual optical assembly.
class Beam {
 public:
  static constexpr uint64_t clearUs=20000, breakUs=200;
  void edge(bool broken,uint64_t at) {
    advance(at);
    if (broken!=broken_) { broken_=broken; changed_=at; emitted_=false; }
  }
  void advance(uint64_t now) {
    if (broken_ && !emitted_ && now-changed_>=breakUs) {
      if (!event_) { event_=true; eventAt_=changed_; }
      emitted_=true;
    }
  }
  bool clear(uint64_t now) const { return !broken_ && now-changed_>=clearUs; }
  bool take(uint64_t& at) {
    if (!event_) return false;
    at=eventAt_; event_=false; return true;
  }
 private:
  bool broken_=true, emitted_=true, event_=false;
  uint64_t changed_=0, eventAt_=0;
};
}
