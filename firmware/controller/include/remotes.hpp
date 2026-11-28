#pragma once
#include <stdint.h>
#include <string.h>
#include <lanetime/protocol.hpp>
#include <lanetime/command.hpp>
namespace jns {
// What ST-1 should do once a remote's command is accepted.
enum class RemoteAction : uint8_t { None, Arm, Go, Cancel, Reveal };
// ST-1's state as far as judging a command needs it.
struct RemoteView {
  uint64_t session=0;
  uint32_t attempt=0;
  bool active=false;   // An attempt is in progress: control cannot change hands.
  bool armable=false;  // An ARM now would be accepted.
  bool canGo=false;    // Armed on a Standing start.
  bool canReveal=false;
};
struct RemoteOutcome {
  bool replies=false;  // Whether ST-1 answers this packet.
  Reply reply=Reply::Request;
  Reason reason=Reason::None;
  RemoteAction action=RemoteAction::None;
  uint8_t slot=0;
  uint16_t arg=0;
};
// Paired remotes, who holds control, and the rules from the remote design:
// CANCEL from any paired remote at any time; ARM, GO and REVEAL only from the holder;
// control is taken by request when free or when the holder has gone quiet, never mid-attempt;
// presses older than kCommandMaxAgeMs are refused; a repeated sequence number is never acted on twice.
class Remotes {
 public:
  enum : uint8_t { kSlots=4 };
  static constexpr uint64_t kSilentUs=30000000, kPairWindowUs=30000000, kPresentUs=5000000;
  bool paired(uint8_t slot) const { return valid(slot) && slots_[slot-1].used; }
  const uint8_t* mac(uint8_t slot) const { return paired(slot)?slots_[slot-1].mac:nullptr; }
  uint8_t slotOf(const uint8_t* mac) const {
    for (uint8_t i=0;i<kSlots;++i) if (slots_[i].used && !memcmp(slots_[i].mac,mac,6)) return uint8_t(i+1);
    return 0;
  }
  uint8_t count() const { uint8_t n=0; for (auto& s:slots_) n+=s.used; return n; }
  uint8_t holder() const { return holder_; }
  // Battery percent last reported (255 = unknown), and whether it has been heard lately.
  uint8_t battery(uint8_t slot) const { return paired(slot)?slots_[slot-1].battery:255; }
  bool present(uint8_t slot,uint64_t now) const { return paired(slot) && heard(slot,now,kPresentUs); }
  // CANCEL on ST-1 itself releases remote control.
  void release() { holder_=0; }
  void forget() { for (auto& s:slots_) s=Slot(); holder_=0; candidateValid_=false; }
  // Persisted form: one 6-byte address per slot, and a bit mask of used slots.
  void restore(const uint8_t* macs,uint8_t used) {
    forget();
    for (uint8_t i=0;i<kSlots;++i) if (used&(1U<<i)) { slots_[i].used=true; memcpy(slots_[i].mac,macs+6*i,6); }
  }
  uint8_t save(uint8_t* macs) const {
    uint8_t used=0; memset(macs,0,6*kSlots);
    for (uint8_t i=0;i<kSlots;++i) if (slots_[i].used) { used|=uint8_t(1U<<i); memcpy(macs+6*i,slots_[i].mac,6); }
    return used;
  }
  // Pairing: ST-1 opens a window; the first remote asking is shown for confirmation on ST-1.
  void openPairing(uint64_t now) { pairUntil_=now+kPairWindowUs; candidateValid_=false; }
  void closePairing() { pairUntil_=0; candidateValid_=false; }
  bool pairing(uint64_t now) const { return pairUntil_ && now<pairUntil_; }
  bool candidate(uint8_t (&mac)[6]) const { if (candidateValid_) memcpy(mac,candidate_,6); return candidateValid_; }
  // Assigns a slot to the candidate (its old one if already paired). 0 if none is free.
  uint8_t confirm() {
    if (!candidateValid_) return 0;
    uint8_t slot=slotOf(candidate_);
    if (!slot) for (uint8_t i=0;i<kSlots;++i) if (!slots_[i].used) { slot=uint8_t(i+1); break; }
    if (slot) { Slot& s=slots_[slot-1]; s=Slot(); s.used=true; memcpy(s.mac,candidate_,6); }
    closePairing();
    return slot;
  }
  RemoteOutcome handle(const uint8_t* mac,const Packet& p,const Command& c,const RemoteView& v,uint64_t now) {
    RemoteOutcome o;
    if (p.role!=Role::Remote || p.kind!=Kind::Command || c.reply!=Reply::Request) return o;
    if (c.code==Code::Pair) {
      if (pairing(now) && (!candidateValid_ || !memcmp(candidate_,mac,6))) { memcpy(candidate_,mac,6); candidateValid_=true; }
      return o;  // Answered only when someone confirms on ST-1.
    }
    o.slot=slotOf(mac);
    if (!o.slot) { o.replies=true; o.reply=Reply::Refused; o.reason=Reason::NotPaired; return o; }
    Slot& s=slots_[o.slot-1];
    s.heard=now; s.seen=true;
    if (c.code==Code::Hello) { s.battery=uint8_t(c.arg>100?255:c.arg); return o; }
    // The same press retried: answer it again, act on it once. An older press: refuse.
    if (s.havePress && s.boot==p.boot) {
      if (c.seq==s.seq) { RemoteOutcome again=s.last; again.action=RemoteAction::None; return again; }
      if (c.seq<s.seq) return refuse(o,Reason::TooLate);
    }
    s.havePress=true; s.boot=p.boot; s.seq=c.seq;
    o.replies=true; o.reply=Reply::Refused;
    if (c.code==Code::Cancel) { o.reply=Reply::Accepted; o.action=RemoteAction::Cancel; }
    else if (c.arg>kCommandMaxAgeMs) o.reason=Reason::TooLate;
    else if (p.session!=v.session) o.reason=Reason::WrongSession;
    else if (c.code==Code::TakeControl) {
      if (v.active) o.reason=Reason::Busy;
      else if (!holder_ || holder_==o.slot || !heard(holder_,now,kSilentUs)) { holder_=o.slot; o.reply=Reply::Accepted; }
      else o.reason=Reason::NotInControl;
    } else if (c.code==Code::Arm || c.code==Code::Go || c.code==Code::Reveal) {
      if (holder_!=o.slot) o.reason=Reason::NotInControl;
      else if (p.attempt!=v.attempt) o.reason=Reason::WrongAttempt;
      else if (c.code==Code::Arm && !v.armable) o.reason=Reason::NotReady;
      else if (c.code==Code::Go && !v.canGo) o.reason=Reason::NotReady;
      else if (c.code==Code::Reveal && !v.canReveal) o.reason=Reason::NotReady;
      else {
        o.reply=Reply::Accepted;
        o.action=c.code==Code::Arm?RemoteAction::Arm:c.code==Code::Go?RemoteAction::Go:RemoteAction::Reveal;
      }
    } else o.reason=Reason::Bad;
    s.last=o;
    return o;
  }
  // ST-1 tried the action and it did not happen after all: record the refusal for retries.
  void amend(uint8_t slot,Reason why) {
    if (!paired(slot)) return;
    RemoteOutcome& last=slots_[slot-1].last;
    last.reply=Reply::Refused; last.reason=why; last.action=RemoteAction::None;
  }
 private:
  struct Slot {
    bool used=false, seen=false, havePress=false;
    uint8_t mac[6]={}, battery=255;
    uint32_t boot=0, seq=0;
    uint64_t heard=0;
    RemoteOutcome last;
  };
  static bool valid(uint8_t slot) { return slot>=1 && slot<=kSlots; }
  bool heard(uint8_t slot,uint64_t now,uint64_t within) const {
    const Slot& s=slots_[slot-1];
    return s.seen && now>=s.heard && now-s.heard<within;
  }
  RemoteOutcome refuse(RemoteOutcome o,Reason why) const { o.replies=true; o.reply=Reply::Refused; o.reason=why; return o; }
  Slot slots_[kSlots];
  uint8_t holder_=0;
  uint64_t pairUntil_=0;
  uint8_t candidate_[6]={};
  bool candidateValid_=false;
};
}
