#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
// A presentation ends only after 500ms of confirmed no-tag observations.
// Communication errors and collisions must NOT count as absence.
class Presentation {
 public:
  bool observe(const uint8_t* uid, size_t size, uint32_t) {
    if (!uid || (size != 4 && size != 7 && size != 10)) return false;
    missing_ = false;
    if (occupied_) return false; // Require removal even when another UID appears.
    memcpy(uid_, uid, size); size_ = size; occupied_ = true;
    return true;
  }
  void absent(uint32_t now) {
    if (!missing_) { missing_ = true; missingSince_ = now; }
    if (uint32_t(now - missingSince_) >= 500) { occupied_ = false; size_ = 0; }
  }
  void uncertain() { missing_ = false; }
  size_t size() const { return size_; }
  const uint8_t* uid() const { return uid_; }
 private:
  uint8_t uid_[10]{};
  size_t size_ = 0;
  bool occupied_ = false, missing_ = false;
  uint32_t missingSince_ = 0;
};
