#pragma once
#include <stdint.h>
#include <string.h>
#include <initializer_list>
#include <lanetime/protocol.hpp>
namespace jns {
enum class State { Idle, Preparing, Armed, Countdown, Running, Revealing, Complete, Fault };
constexpr uint64_t kPeerUs=3000000, kAttemptUs=30000000;
constexpr uint64_t kRevealLeadUs=1000000, kRevealRepeatUs=100000;
// Standing cue schedule: lead-in for radio delivery, then three beeps 1 s apart and GO.
constexpr uint64_t kCueLeadUs=1000000, kCueStepUs=1000000;
constexpr uint8_t kCueBeeps=3;
struct Peer {
  bool used=false;
  uint8_t mac[6]={};
  Packet packet;
  uint64_t seen=0;
};
class Controller {
 public:
  Controller(uint32_t system,uint64_t session):system_(system),session_(session) {}
  State state=State::Idle;
  Mode mode=Mode::Flying;
  uint8_t lanes=1, started=0, finished=0;
  uint32_t attempt=0;
  uint64_t results[4]={}, since=0, goAt=0, startAt=0, revealAt=0;
  const char* reason="Ready to configure";
  uint8_t mask() const { return uint8_t((1U<<lanes)-1); }
  bool active() const {
    return state==State::Preparing || state==State::Armed || state==State::Countdown || state==State::Running || state==State::Revealing;
  }
  void cancel() { state=State::Idle; started=finished=0; goAt=startAt=revealAt=0; reason="Cancelled / idle"; }
  void fail(const char* why) { state=State::Fault; revealAt=0; reason=why; }
  // Freeze this attempt's results/mask. Late results cannot change a scheduled reveal.
  bool reveal(uint64_t now) {
    if (state!=State::Running) return false;
    if (now-since>kAttemptUs) { fail("Attempt timeout / DNF"); return false; }
    revealAt=now+kRevealLeadUs; state=State::Revealing;
    reason="Results held - revealing shortly"; return true;
  }
  bool relevant(const Packet& p) const {
    // Start sensors carry the countdown cues in Standing, so they are required in both modes.
    return p.lane<=lanes && (p.role==Role::Finish || p.role==Role::Start);
  }
  bool conflict(uint64_t now) const {
    for (const auto& a:peers_) {
      if (!a.used || now-a.seen>kPeerUs) continue;
      if (a.packet.role==Role::Controller || (a.packet.role==Role::Start && a.packet.lane==1)) return true;
      for (const auto& b:peers_) if (&a!=&b && b.used && now-b.seen<=kPeerUs &&
          a.packet.role==b.packet.role && a.packet.lane==b.packet.lane) return true;
    }
    return false;
  }
  const char* readiness(uint64_t now,bool clear,bool acknowledgements) const {
    if (conflict(now)) return "Duplicate role / controller";
    if (mode==Mode::Flying && (!acknowledgements || !(started&1)) && !clear) return "Lane 1 beam not clear";
    for (uint8_t lane=1;lane<=lanes;++lane) for (Role role:{Role::Start,Role::Finish}) {
      if (role==Role::Start && lane==1) continue;
      const Peer* found=find(now,role,lane);
      if (!found) return "Waiting for sensor status";
      const auto& p=found->packet;
      if (p.flags&jns::Fault) return "Sensor reports fault";
      if (!acknowledgements && !(p.flags&jns::Ready)) return "Sensor beam not ready";
      if (acknowledgements && (p.session!=session_ || p.attempt!=attempt || p.mode!=mode ||
          p.mask!=mask() || !(p.flags&jns::Armed))) return "Waiting for ARM acknowledgement";
    }
    return nullptr;
  }
  // Every required peer holds this attempt's GO time.
  bool cued(uint64_t now) const {
    for (uint8_t lane=1;lane<=lanes;++lane) for (Role role:{Role::Start,Role::Finish}) {
      if (role==Role::Start && lane==1) continue;
      const Peer* x=find(now,role,lane);
      if (!x || x->packet.session!=session_ || x->packet.attempt!=attempt ||
          !(x->packet.flags&jns::Cued) || x->packet.value!=goAt) return false;
    }
    return true;
  }
  uint64_t firstCue() const { return goAt-kCueBeeps*kCueStepUs; }
  // FN-1 has accepted ST-1's START; until then ST-1 keeps resending it.
  bool startPending(uint64_t now) const {
    if (mode!=Mode::Flying || !(started&1) || state!=State::Running) return false;
    const Peer* x=find(now,Role::Finish,1);
    return !x || x->packet.session!=session_ || x->packet.attempt!=attempt ||
      !(x->packet.flags&(jns::Running|jns::Complete));
  }
  bool arm(uint64_t now,bool clear) {
    if (active()) return false;
    if (now<kPeerUs) { reason="Discovering sensors"; return false; }
    if (const char* why=readiness(now,clear,false)) { reason=why; return false; }
    if (attempt==UINT32_MAX) { fail("Attempt exhausted - reboot"); return false; }
    ++attempt; started=finished=0; memset(results,0,sizeof(results)); since=now; goAt=startAt=revealAt=0;
    state=State::Preparing; reason="Preparing sensors"; return true;
  }
  bool observe(const uint8_t* mac,const Packet& p,uint64_t now) {
    if (p.system!=system_) return false;
    if (active() && state!=State::Revealing && now>=since && now-since>kAttemptUs) { fail("Attempt timeout / DNF"); return false; }
    if (p.kind==Kind::Status) {
      Peer* slot=nullptr;
      for (auto& x:peers_) if (x.used && memcmp(x.mac,mac,6)==0) { slot=&x; break; }
      if (!slot && active() && state!=State::Preparing && relevant(p)) fail("New sensor during attempt");
      if (slot && active() && (slot->packet.boot!=p.boot || slot->packet.role!=p.role || slot->packet.lane!=p.lane))
        fail("Peer reboot / identity changed");
      if (!slot) for (auto& x:peers_) if (!x.used || now-x.seen>kPeerUs) { slot=&x; break; }
      if (!slot) { fail("Peer table full"); return false; }
      slot->used=true; memcpy(slot->mac,mac,6); slot->packet=p; slot->seen=now;
      if (active() && conflict(now)) fail("Duplicate role / controller");
      return true;
    }
    if (!active() || p.session!=session_ || p.attempt!=attempt || p.mode!=mode || p.mask!=mask() || !relevant(p)) return false;
    const Peer* known=nullptr;
    for (const auto& x:peers_) if (x.used && memcmp(x.mac,mac,6)==0 && now-x.seen<=kPeerUs &&
        x.packet.boot==p.boot && x.packet.role==p.role && x.packet.lane==p.lane &&
        x.packet.session==session_ && x.packet.attempt==attempt && (x.packet.flags&jns::Armed)) known=&x;
    if (!known) return false;
    const uint8_t bit=uint8_t(1U<<(p.lane-1));
    if (p.kind==Kind::Start && p.role==Role::Start && mode==Mode::Flying && p.lane>1) {
      if (state==State::Preparing) { fail("Start before group ready"); return false; }
      if (state!=State::Armed && state!=State::Running) return false;
      started|=bit; state=State::Running; return true;
    }
    if (p.kind==Kind::Result && p.role==Role::Finish && (p.flags&jns::Complete) && !(p.flags&jns::Fault) &&
        (started&bit) && !(finished&bit) && p.value>0 && p.value<=kAttemptUs && state==State::Running) {
      results[p.lane-1]=p.value; finished|=bit;
      return true;
    }
    return false;
  }
  void tick(uint64_t now,bool clear) {
    if (!active()) return;
    if (conflict(now)) { fail("Duplicate role / controller"); return; }
    if (state==State::Preparing) {
      if (mode==Mode::Flying && !clear) { fail("Beam broke while preparing"); return; }
      if (!readiness(now,clear,true)) { state=State::Armed; reason="ARMED - watch LEDs"; }
      else if (now-since>kPeerUs) fail("ARM acknowledgement timeout");
      return;
    }
    // After arming, local beam changes are events rather than readiness failures.
    if (const char* why=readiness(now,true,true)) { fail(why); return; }
    if (state==State::Revealing) {
      if (now>=revealAt) { state=State::Complete; reason="Revealed - prototype times"; }
      return;
    }
    if (now-since>kAttemptUs) { fail("Attempt timeout / DNF"); return; }
    // Use the current loop time, not a queued packet timestamp, for the lead-in.
    if (state==State::Running && finished==mask()) { reveal(now); return; }
    if (state==State::Countdown) {
      if (now>=firstCue() && !cued(now)) { fail("Cue schedule not acknowledged"); return; }
      if (now>=goAt) { started=mask(); state=State::Running; reason="GO - running"; }
    }
  }
  // Fix the GO time in ST-1's clock. Cues sound at GO-3 s, GO-2 s, GO-1 s and GO on every
  // start unit; finish units start their clocks at GO. Packet arrival time is irrelevant.
  bool countdown(uint64_t now) {
    if (state!=State::Armed || mode!=Mode::Standing) return false;
    goAt=now+kCueLeadUs+kCueBeeps*kCueStepUs;
    state=State::Countdown; reason="Countdown"; return true;
  }
  bool localStart(uint64_t at) {
    if (mode!=Mode::Flying || (state!=State::Armed && state!=State::Running) || (started&1)) return false;
    started|=1; startAt=at; state=State::Running; reason="Flying - running"; return true;
  }
 private:
  const Peer* find(uint64_t now,Role role,uint8_t lane) const {
    const Peer* found=nullptr;
    for (const auto& x:peers_) if (x.used && now-x.seen<=kPeerUs && x.packet.role==role && x.packet.lane==lane) found=&x;
    return found;
  }
  uint32_t system_;
  uint64_t session_;
  Peer peers_[12];
};
}
