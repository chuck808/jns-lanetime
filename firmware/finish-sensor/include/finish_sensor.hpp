#pragma once
#include <stdint.h>
#include <string.h>
#include <lanetime/beam.hpp>
#include <lanetime/protocol.hpp>
#include <lanetime/timebase.hpp>
namespace jns {
enum class FinishState { Idle, Armed, Cued, Running, Complete, Stopped, Invalid };
// FN-1 only: lane-one START comes from the bound ST-1, never another start post.
// Single-owner logic; interrupt/radio callbacks must queue events to its owner.
class FinishSensor {
 public:
  FinishSensor(uint32_t system,uint32_t boot):system_(system),boot_(boot) {}
  FinishState state() const { return state_; }
  uint64_t elapsed() const { return elapsed_; }
  bool clear(uint64_t now) const { return beam_.clear(now); }
  bool ready(uint64_t now) const { return !locked_ && online_ && clock_.ready(now) && clear(now); }
  void hardwareFault() { locked_=true; invalidate(); }
  void overflow() { hardwareFault(); } // Lost edges/control messages cannot be reconstructed.
  void edge(bool broken,uint64_t at) {
    beam_.edge(broken,at); takeBreak();
  }
  void tick(uint64_t now) {
    beam_.advance(now); takeBreak();
    if (locked_) return;
    if (online_ && now>=lastStatus_ && now-lastStatus_>3000000) {
      online_=false; clock_=Timebase(); invalidate();
    }
    if (!adopted()) return;
    if (!clock_.ready(now)) { invalidate(); return; }
    if (!sealed_ && state_!=FinishState::Complete && now-armLocal_>30000000) { invalidate(); return; }
    if (sealed_ || !haveStart_ || state_==FinishState::Complete) return;
    uint64_t remoteNow=0; clock_.toRemote(now,remoteNow);
    if (mode_==Mode::Standing && remoteNow<startRemote_) return;
    state_=FinishState::Running;
    while (count_ && clock_.interpolable(breaks_[0])) {
      uint64_t finishRemote=0; clock_.toRemote(breaks_[0],finishRemote);
      for (uint8_t i=1;i<count_;++i) breaks_[i-1]=breaks_[i];
      --count_;
      if (finishRemote<=startRemote_) continue; // Stray break before start.
      const uint64_t elapsed=finishRemote-startRemote_;
      if (elapsed>30000000) { invalidate(); return; }
      elapsed_=elapsed; state_=FinishState::Complete; count_=0; return;
    }
  }
  // p has already passed shared decode(). at is the receive callback timestamp.
  void receive(const uint8_t* mac,const Packet& p,uint64_t at) {
    if (locked_ || p.system!=system_ || !p.boot) return;
    // A second FN-1 cannot safely share this set, even before adopting a session.
    if (p.role==Role::Finish && p.lane==1 && p.kind==Kind::Status) { hardwareFault(); return; }
    if (p.role!=Role::Controller || p.lane!=1 || !p.session) return;
    if (!bound_) {
      if (p.kind!=Kind::Status) return;
      memcpy(controller_,mac,6); bound_=true;
    } else if (memcmp(controller_,mac,6)) {
      if (p.kind==Kind::Status) hardwareFault();
      return;
    }
    if (session_!=p.session) {
      if (p.kind!=Kind::Status) return;
      for (uint8_t i=0;i<retiredCount_;++i) if (retiredSessions_[i]==p.session) return;
      if (session_) {
        if (retiredCount_==8) { hardwareFault(); return; }
        retiredSessions_[retiredCount_++]=session_;
      }
      session_=p.session; controllerBoot_=p.boot; clock_=Timebase(); online_=false; lastRemote_=0;
      // Retire the attempt already in progress at discovery/reboot; never resume it.
      attempt_=p.attempt; mode_=p.mode; mask_=p.mask; resetAttempt(); state_=FinishState::Idle;
    }
    if (p.boot!=controllerBoot_) { hardwareFault(); return; }
    if (p.kind==Kind::Status) {
      if (!p.value || p.value<=lastRemote_) return;
      if (online_ && at>=lastStatus_ && at-lastStatus_>3000000) { clock_=Timebase(); invalidate(); }
      lastRemote_=p.value; lastStatus_=at; online_=true; clock_.sample(session_,at,p.value);
      if (p.attempt<attempt_) return;
      if ((p.flags&jns::Fault) && p.attempt>=attempt_) { attempt_=p.attempt; invalidate(); return; }
      if (adopted() && (p.attempt>attempt_ || p.mode!=mode_ || p.mask!=mask_)) invalidate();
      if (!adopted()) {
        mode_=p.mode; mask_=p.mask;
        if (!(p.flags&(jns::Armed|jns::Running|jns::Complete|jns::Fault))) state_=FinishState::Idle;
      }
      return;
    }
    if (!online_ || p.attempt<attempt_) return;
    if (p.kind==Kind::Cancel) { attempt_=p.attempt; invalidate(); state_=FinishState::Idle; return; }
    if (p.kind==Kind::Arm) {
      if (!p.attempt || p.attempt<=attempt_) return;
      // Consume this attempt even if not ready; duplicates cannot reopen a rejected arm.
      attempt_=p.attempt; mode_=p.mode; mask_=p.mask; resetAttempt();
      if (!(mask_&1) || !ready(at)) { invalidate(); return; }
      armLocal_=at; clock_.toRemote(at,armRemote_); state_=FinishState::Armed; return;
    }
    if (p.attempt!=attempt_ || p.mode!=mode_ || p.mask!=mask_ || !adopted()) return;
    if (p.kind==Kind::Reveal) {
      if (sealed_) return;
      sealed_=true; count_=0;
      // Preserve Armed acknowledgement through ST-1's reveal lead-in without inventing a Result.
      if (state_!=FinishState::Complete) state_=FinishState::Stopped;
      if (!(p.finishedMask&1)) elapsed_=0;
      return;
    }
    if (sealed_ || state_==FinishState::Complete) return;
    if ((mode_==Mode::Flying && p.kind==Kind::Start) || (mode_==Mode::Standing && p.kind==Kind::Go)) {
      if (haveStart_) return; // First timestamp is immutable, including conflicting retries.
      uint64_t remoteNow=0;
      if (!clock_.ready(at) || !clock_.toRemote(at,remoteNow) || p.value<armRemote_ || !p.value) {
        invalidate(); return;
      }
      if (mode_==Mode::Standing && (p.value<=remoteNow || p.value-remoteNow>5000000)) { invalidate(); return; }
      // ST-1 START is direct; allow the provisional one-way sync bias (up to 10 ms).
      if (mode_==Mode::Flying && p.value>remoteNow && p.value-remoteNow>10000) { invalidate(); return; }
      haveStart_=true; startRemote_=p.value;
      state_=mode_==Mode::Standing?FinishState::Cued:FinishState::Running;
    }
  }
  Packet status(uint64_t now) const {
    Packet p; p.role=Role::Finish; p.system=system_; p.boot=boot_; p.session=session_;
    p.attempt=attempt_; p.mode=mode_; p.mask=mask_;
    if (ready(now)) p.flags|=jns::Ready;
    if (adopted()) p.flags|=jns::Armed;
    if (haveStart_) p.flags|=jns::Running;
    if (state_==FinishState::Cued) p.flags&=~jns::Running;
    if (state_==FinishState::Complete) p.flags|=jns::Complete;
    if (locked_ || state_==FinishState::Invalid) p.flags|=jns::Fault;
    // Recoverable invalid attempts advertise Ready for a fresh ARM only after CANCEL/idle status.
    if (p.flags&jns::Fault) p.flags&=~jns::Ready;
    if (haveStart_ && mode_==Mode::Standing) { p.flags|=jns::Cued; p.value=startRemote_; }
    return p;
  }
  bool result(uint64_t now,Packet& p) const {
    if (state_!=FinishState::Complete || !elapsed_ || !online_ || !clock_.ready(now)) return false;
    p=status(now); p.kind=Kind::Result; p.value=elapsed_; return true;
  }
 private:
  bool adopted() const { return state_==FinishState::Armed || state_==FinishState::Cued ||
      state_==FinishState::Running || state_==FinishState::Complete || state_==FinishState::Stopped; }
  void resetAttempt() { haveStart_=sealed_=false; count_=0; elapsed_=startRemote_=0; }
  void invalidate() { resetAttempt(); state_=FinishState::Invalid; }
  void takeBreak() {
    uint64_t at=0; if (!beam_.take(at) || !adopted() || sealed_ || state_==FinishState::Complete || at<armLocal_) return;
    if (count_==8) { invalidate(); return; }
    breaks_[count_++]=at;
  }
  uint32_t system_,boot_,controllerBoot_=0,attempt_=0;
  uint8_t controller_[6]={},mask_=1,count_=0,retiredCount_=0;
  uint64_t session_=0,retiredSessions_[8]={},lastStatus_=0,lastRemote_=0,armLocal_=0,armRemote_=0,
      startRemote_=0,elapsed_=0,breaks_[8]={};
  bool bound_=false,online_=false,locked_=false,haveStart_=false,sealed_=false;
  Mode mode_=Mode::Flying;
  FinishState state_=FinishState::Idle;
  Timebase clock_;
  Beam beam_;
};
}
