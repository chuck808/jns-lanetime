#pragma once
#include <stdint.h>
#include <string.h>
#include <lanetime/beam.hpp>
#include <lanetime/protocol.hpp>
#include <lanetime/timebase.hpp>
namespace jns {
enum class StartState { Idle, Armed, Cued, Running, Stopped, Invalid };
// External start posts only (lanes 2–4); ST-1 owns lane one.
// Single-owner logic; interrupt/radio callbacks must queue events to its owner.
class StartSensor {
 public:
  StartSensor(uint32_t system,uint32_t boot,uint8_t lane):system_(system),boot_(boot),lane_(lane) {
    if (lane<2 || lane>4) hardwareFault();
  }
  StartState state() const { return state_; }
  uint64_t goLocal() const { return goLocal_; }
  bool cuesActive() const { return haveStart_ && mode_==Mode::Standing && !sealed_ && adopted(); }
  bool clear(uint64_t now) const { return beam_.clear(now); }
  bool ready(uint64_t now) const { return !locked_ && online_ && clock_.ready(now) && (mode_==Mode::Standing || clear(now)); }
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
    if (!sealed_ && now-armLocal_>30000000) { invalidate(); return; }
    if (sealed_) return;
    if (mode_==Mode::Standing) {
      if (haveStart_ && now>=goLocal_) state_=StartState::Running;
    } else if (eventLocal_ && !haveStart_ && clock_.interpolable(eventLocal_)) {
      clock_.toRemote(eventLocal_,startRemote_);
      if (startRemote_<armRemote_ || !startRemote_) { invalidate(); return; }
      haveStart_=true; state_=StartState::Running;
    }
  }

  // p has already passed shared decode(). at is the receive callback timestamp.
  void receive(const uint8_t* mac,const Packet& p,uint64_t at) {
    if (locked_ || p.system!=system_ || !p.boot) return;
    // Another start post with our lane is a latched identity conflict.
    if (p.role==Role::Start && p.lane==lane_ && p.kind==Kind::Status) { hardwareFault(); return; }
    if (p.role==Role::Finish && p.lane==lane_ && p.kind==Kind::Status) {
      if (p.session!=session_ || p.attempt!=attempt_ || p.mode!=mode_ || p.mask!=mask_ || !adopted()) return;
      if (finishBound_ && (memcmp(finishMac_,mac,6) || finishBoot_!=p.boot)) { invalidate(); return; }
      if (!(p.flags&jns::Armed)) return;
      if (!finishBound_) { memcpy(finishMac_,mac,6); finishBoot_=p.boot; finishBound_=true; }
      if (p.flags&jns::Fault) { invalidate(); return; }
      if (haveStart_ && (p.flags&(jns::Running|jns::Complete))) acknowledged_=true;
      return;
    }
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
      attempt_=p.attempt; mode_=p.mode; mask_=p.mask; resetAttempt(); state_=StartState::Idle;
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
        if (!(p.flags&(jns::Armed|jns::Running|jns::Complete|jns::Fault))) state_=StartState::Idle;
      }
      return;
    }
    if (!online_ || p.attempt<attempt_) return;
    if (p.kind==Kind::Cancel) { attempt_=p.attempt; invalidate(); state_=StartState::Idle; return; }
    if (p.kind==Kind::Arm) {
      if (!p.attempt || p.attempt<=attempt_) return;
      // Consume this attempt even if not ready; duplicates cannot reopen a rejected arm.
      attempt_=p.attempt; mode_=p.mode; mask_=p.mask; resetAttempt();
      if (!(mask_&laneBit()) || !ready(at)) { invalidate(); return; }
      armLocal_=at; clock_.toRemote(at,armRemote_); state_=StartState::Armed; return;
    }
    if (p.attempt!=attempt_ || p.mode!=mode_ || p.mask!=mask_ || !adopted()) return;
    if (p.kind==Kind::Reveal) {
      sealed_=true; state_=StartState::Stopped; return;
    }
    if (sealed_ || haveStart_) return;
    if (mode_==Mode::Standing && p.kind==Kind::Go) {
      uint64_t remoteNow=0;
      if (!clock_.ready(at) || !clock_.toRemote(at,remoteNow) || p.value<=remoteNow ||
          p.value-remoteNow>5000000 || p.value<3000000) { invalidate(); return; }
      // Freeze a local schedule once. Never replay a countdown received after its first cue.
      clock_.toLocal(p.value,goLocal_);
      uint64_t first=0; clock_.toLocal(p.value-3000000,first);
      if (first<=at+20000) { invalidate(); return; }
      for (uint8_t i=0;i<8;++i) {
        const uint8_t tone=i/2;
        const uint64_t remote=p.value-(3-tone)*1000000ULL+(i%2?(tone==3?400000:120000):0);
        clock_.toLocal(remote,cueTimes_[i]);
      }
      haveStart_=true; startRemote_=p.value; state_=StartState::Cued;
    }
  }
  uint64_t cueTime(uint8_t index) const { return index<8?cueTimes_[index]:0; }

  Packet status(uint64_t now) const {
    Packet p; p.role=Role::Start; p.lane=lane_; p.system=system_; p.boot=boot_; p.session=session_;
    p.attempt=attempt_; p.mode=mode_; p.mask=mask_;
    if (ready(now)) p.flags|=jns::Ready;
    if (adopted()) p.flags|=jns::Armed;
    if (haveStart_) p.flags|=jns::Running;
    if (state_==StartState::Cued) p.flags&=~jns::Running;
    if (locked_ || state_==StartState::Invalid) p.flags|=jns::Fault;
    // Recoverable invalid attempts advertise Ready for a fresh ARM only after CANCEL/idle status.
    if (p.flags&jns::Fault) p.flags&=~jns::Ready;
    if (haveStart_ && mode_==Mode::Standing) { p.flags|=jns::Cued; p.value=startRemote_; }
    return p;
  }
  bool start(uint64_t now,Packet& p) const {
    if (mode_!=Mode::Flying || !haveStart_ || sealed_ || acknowledged_ ||
        state_!=StartState::Running || !online_ || !clock_.ready(now)) return false;
    p=status(now); p.kind=Kind::Start; p.value=startRemote_; return true;
  }
 private:
  uint8_t laneBit() const { return uint8_t(1U<<(lane_-1)); }
  bool adopted() const { return state_==StartState::Armed || state_==StartState::Cued ||
      state_==StartState::Running || state_==StartState::Stopped; }
  void resetAttempt() {
    haveStart_=sealed_=acknowledged_=finishBound_=false;
    eventLocal_=startRemote_=goLocal_=0;
  }
  void invalidate() { resetAttempt(); state_=StartState::Invalid; }
  void takeBreak() {
    uint64_t at=0;
    if (!beam_.take(at) || state_!=StartState::Armed || mode_!=Mode::Flying ||
        eventLocal_ || at<armLocal_) return;
    eventLocal_=at; // First qualified edge is immutable, even while interpolation is pending.
  }
  uint32_t system_,boot_,controllerBoot_=0,attempt_=0;
  uint8_t lane_,controller_[6]={},finishMac_[6]={},mask_=1,retiredCount_=0;
  uint32_t finishBoot_=0;
  uint64_t session_=0,retiredSessions_[8]={},lastStatus_=0,lastRemote_=0,armLocal_=0,armRemote_=0,
      startRemote_=0,eventLocal_=0,goLocal_=0,cueTimes_[8]={};
  bool bound_=false,online_=false,locked_=false,haveStart_=false,sealed_=false,acknowledged_=false,finishBound_=false;
  Mode mode_=Mode::Flying;
  StartState state_=StartState::Idle;
  Timebase clock_;
  Beam beam_;
};
}
