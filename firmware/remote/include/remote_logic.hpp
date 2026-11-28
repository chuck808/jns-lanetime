#pragma once
#include <stdint.h>
#include <lanetime/protocol.hpp>
#include <lanetime/command.hpp>
namespace jns {
// What the remote last heard from its paired ST-1. The light only ever mirrors this.
struct Mirror {
  bool heard=false;
  uint64_t at=0;          // Local time of the last Status.
  uint64_t session=0;
  uint32_t attempt=0, boot=0;
  Mode mode=Mode::Flying;
  uint8_t mask=1, flags=0, control=0;  // control = Status byte 27.
  void take(const Packet& p,uint64_t now) {
    heard=true; at=now; session=p.session; attempt=p.attempt; boot=p.boot; mode=p.mode;
    mask=p.mask; flags=p.flags; control=p.finishedMask;
  }
  bool fresh(uint64_t now) const { return heard && now>=at && now-at<2000000; }
  bool inControl(uint8_t slot) const { return slot && (control&kHolderMask)==slot; }
};
enum class Glow : uint8_t { Off, Searching, Pairing, NotInControl, NotReady, Ready, Armed, Running, Revealed, Fault };
inline Glow glowFor(const Mirror& m,uint8_t slot,uint64_t now) {
  if (!m.fresh(now)) return Glow::Searching;
  if (m.control&kControllerFault) return Glow::Fault;
  if (!m.inControl(slot)) return Glow::NotInControl;
  if (m.flags&Complete) return Glow::Revealed;
  if (m.flags&Running) return Glow::Running;
  if (m.flags&Armed) return Glow::Armed;
  if (m.flags&Ready) return Glow::Ready;
  return Glow::NotReady;
}
// RGB at full scale; the firmware applies brightness. lit=false for the dark half of a pulse.
struct Rgb { uint8_t r, g, b; };
inline Rgb colourFor(Glow g,uint64_t now) {
  const bool slow=(now/600000)%2==0;
  switch (g) {
    case Glow::Off: return Rgb{0,0,0};
    case Glow::Searching: return slow?Rgb{110,110,110}:Rgb{20,20,20};
    case Glow::Pairing: return slow?Rgb{0,180,200}:Rgb{0,30,40};
    case Glow::NotInControl: return Rgb{150,80,255};
    case Glow::NotReady: return Rgb{240,150,20};
    case Glow::Ready: return Rgb{40,90,20};
    case Glow::Armed: return Rgb{110,230,60};
    case Glow::Running: return Rgb{40,110,255};
    case Glow::Revealed: return Rgb{230,230,230};
    case Glow::Fault: return Rgb{240,40,30};
  }
  return Rgb{0,0,0};
}
// What a short press of ARM / GO asks for, given the state shown. Code 0 = nothing to send.
// Not in control: nothing (hold 2 s to take control). Running: nothing (hold 1 s to reveal).
inline uint8_t pressCode(const Mirror& m,uint8_t slot,uint64_t now) {
  if (!m.fresh(now) || !m.inControl(slot) || (m.control&kControllerFault)) return 0;
  if (m.flags&Running) return 0;
  if (m.flags&Armed) return m.mode==Mode::Standing?uint8_t(Code::Go):0;
  return uint8_t(Code::Arm);
}
// A held ARM / GO: reveal early while running (1 s), take control while idle (2 s).
inline uint8_t holdCode(const Mirror& m,uint8_t slot,uint64_t now,uint64_t heldUs) {
  if (!m.fresh(now)) return 0;
  if ((m.flags&Running) && m.inControl(slot) && heldUs>=1000000) return uint8_t(Code::Reveal);
  if (!(m.flags&(Armed|Running)) && !m.inControl(slot) && heldUs>=2000000) return uint8_t(Code::TakeControl);
  return 0;
}
// One command in flight: resent every 40 ms until ST-1 answers or it is too old to matter.
// CANCEL keeps trying for a second; anything else gives up at the age ST-1 would refuse.
class Pending {
 public:
  static constexpr uint64_t kResendUs=40000;
  void start(Code code,uint32_t seq,uint64_t pressedAt) { active_=true; code_=code; seq_=seq&kSeqMask; at_=pressedAt; last_=0; }
  bool active() const { return active_; }
  Code code() const { return code_; }
  uint32_t seq() const { return seq_; }
  uint16_t ageMs(uint64_t now) const { const uint64_t ms=now>at_?(now-at_)/1000:0; return uint16_t(ms>60000?60000:ms); }
  // True when the command should be (re)sent now.
  bool due(uint64_t now) {
    if (!active_) return false;
    const uint64_t limit=code_==Code::Cancel?1000000:uint64_t(kCommandMaxAgeMs)*1000;
    if (now>at_ && now-at_>limit) { active_=false; expired_=true; return false; }
    if (last_ && now-last_<kResendUs) return false;
    last_=now; return true;
  }
  // A reply for this command ends it. Returns true if it was ours.
  bool answer(const Command& c) {
    if (!active_ || c.code!=code_ || c.seq!=seq_ || c.reply==Reply::Request) return false;
    active_=false; return true;
  }
  bool takeExpired() { const bool e=expired_; expired_=false; return e; }
 private:
  bool active_=false, expired_=false;
  Code code_=Code::Hello;
  uint32_t seq_=0;
  uint64_t at_=0, last_=0;
};
// Pure button logic (as on ST-1): a press edge, a hold time, and whether the press
// may act. After a wake from sleep the waking press must be released first.
class Button {
 public:
  bool update(bool down,uint64_t now) {
    if (down!=raw_) { raw_=down; changed_=now; }
    if (raw_!=stable_ && now-changed_>=30000) {
      stable_=raw_;
      if (stable_) { pressedAt_=now; fired_=false; if (!blocked_) return true; }
      else blocked_=false;
    }
    return false;
  }
  bool down() const { return stable_ && !blocked_; }
  uint64_t heldFor(uint64_t now) const { return down() && now>pressedAt_?now-pressedAt_:0; }
  bool fireOnce() { if (fired_) return false; fired_=true; return true; }
  // Ignore the current press until it is released (the press that woke the remote).
  void block() { blocked_=true; }
 private:
  bool raw_=false, stable_=false, fired_=false, blocked_=false;
  uint64_t changed_=0, pressedAt_=0;
};
// Rough LiPo charge from voltage, 3.30 V empty to 4.15 V full.
inline uint8_t batteryPercent(uint32_t mv) {
  if (mv<=3300) return 0;
  if (mv>=4150) return 100;
  return uint8_t((mv-3300)*100/850);
}
}
