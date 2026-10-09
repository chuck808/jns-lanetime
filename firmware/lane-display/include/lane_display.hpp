#pragma once
#include <stdint.h>
#include <string.h>
#include <lanetime/protocol.hpp>
#include <lanetime/timebase.hpp>
namespace jns {
enum class DisplayState { Offline, Idle, Disabled, Pending, Time, Dnf, Unavailable, Fault };
// Single-owner pure logic. Only controller Status refreshes link liveness.
class LaneDisplay {
 public:
  LaneDisplay(uint32_t system,uint8_t lane):system_(system),lane_(lane) {
    if (lane<1 || lane>4) lock();
  }
  DisplayState state() const { return state_; }
  uint64_t elapsed() const { return elapsed_; }
  bool synced(uint64_t now) const { return online_ && clock_.ready(now); }
  void overflow() { lock(); }
  void hardwareFault() { lock(); }
  void tick(uint64_t now) {
    if (locked_) return;
    if (online_ && now>=lastStatus_ && now-lastStatus_>3000000) {
      online_=false; clock_=Timebase(); invalidate(); state_=DisplayState::Offline;
    }
    if (state_!=DisplayState::Pending) return;
    if (scheduled_) {
      if (!clock_.ready(now)) { invalidate(); return; }
      if (now>=revealLocal_) {
        state_=!(finished_&bit())?DisplayState::Dnf:
            haveResult_?DisplayState::Time:DisplayState::Unavailable;
        terminal_=true;
      }
    } else if (now>=adoptedAt_ && now-adoptedAt_>32000000) invalidate();
  }
  // at is the receive-callback timestamp. Call tick(current time) before/after draining.
  void receive(const uint8_t* mac,const Packet& p,uint64_t at) {
    if (locked_ || p.system!=system_ || !p.session || !p.boot) return;
    if (p.role==Role::Controller) {
      if (p.lane!=1) return;
      if (!bound_) {
        if (p.kind!=Kind::Status) return;
        memcpy(controller_,mac,6); bound_=true;
      } else if (memcmp(controller_,mac,6)) {
        if (p.kind==Kind::Status) lock();
        return;
      }
      if (p.session!=session_) {
        if (p.kind!=Kind::Status) return;
        for (uint8_t i=0;i<retiredCount_;++i) if (retired_[i]==p.session) return;
        if (session_) {
          if (retiredCount_==8) { lock(); return; }
          retired_[retiredCount_++]=session_;
        }
        session_=p.session; controllerBoot_=p.boot; clock_=Timebase();
        attempt_=0; terminal_=false; scheduled_=haveResult_=peer_=false;
        elapsed_=0; lastRemote_=0; online_=false; state_=DisplayState::Idle;
      }
      if (p.boot!=controllerBoot_) { lock(); return; }
      if (p.kind==Kind::Status) {
        // Ignore delayed/repeated heartbeats, including stale fault/config snapshots.
        if (!p.value || p.value<=lastRemote_) return;
        lastRemote_=p.value; lastStatus_=at; online_=true;
        clock_.sample(session_,at,p.value);
      }
      if (!online_ || p.attempt<attempt_) return;
      if (p.attempt>attempt_) {
        // A Reveal alone must never resurrect a missed/retired attempt.
        if (p.kind!=Kind::Status && p.kind!=Kind::Arm && p.kind!=Kind::Cancel) return;
        attempt_=p.attempt; mode_=p.mode; mask_=p.mask; adoptedAt_=at;
        terminal_=false; scheduled_=haveResult_=peer_=false; elapsed_=0;
        state_=(mask_&bit())?DisplayState::Pending:DisplayState::Disabled;
      }
      if (!attempt_) return;
      if (p.kind==Kind::Cancel || (p.kind==Kind::Status && (p.flags&jns::Fault))) {
        invalidate(); return;
      }
      if (p.mode!=mode_ || p.mask!=mask_) { invalidate(); return; }
      if (terminal_ || state_!=DisplayState::Pending) return;
      if (p.kind==Kind::Reveal) {
        if (scheduled_) {
          if (p.value!=revealRemote_ || p.finishedMask!=finished_) invalidate();
          return;
        }
        if (!clock_.ready(at) || !clock_.toLocal(p.value,revealLocal_) ||
            revealLocal_<=at || revealLocal_-at>2000000) { invalidate(); return; }
        revealRemote_=p.value; finished_=p.finishedMask; scheduled_=true;
      }
      return;
    }
    if (!online_ || terminal_ || state_!=DisplayState::Pending || p.role!=Role::Finish ||
        p.lane!=lane_ || p.session!=session_ || p.attempt!=attempt_ ||
        p.mode!=mode_ || p.mask!=mask_) return;
    if (p.kind==Kind::Status) {
      if (p.flags&jns::Fault) { invalidate(); return; }
      if (!(p.flags&jns::Armed)) return;
      if (peer_ && (memcmp(finish_,mac,6) || finishBoot_!=p.boot)) { invalidate(); return; }
      if (!peer_) { memcpy(finish_,mac,6); finishBoot_=p.boot; peer_=true; }
      finishSeen_=at;
    } else if (p.kind==Kind::Result && peer_ && !memcmp(finish_,mac,6) && finishBoot_==p.boot &&
        at>=finishSeen_ && at-finishSeen_<=3000000 && (p.flags&jns::Complete) &&
        !(p.flags&jns::Fault) && p.value && p.value<=30000000 && !haveResult_) {
      elapsed_=p.value; haveResult_=true;
    }
  }
 private:
  uint8_t bit() const { return uint8_t(1U<<(lane_-1)); }
  void invalidate() {
    terminal_=true; scheduled_=haveResult_=false; elapsed_=0; state_=DisplayState::Unavailable;
  }
  void lock() { invalidate(); locked_=true; state_=DisplayState::Fault; }
  uint32_t system_, attempt_=0, controllerBoot_=0, finishBoot_=0;
  uint8_t lane_, controller_[6]={}, finish_[6]={}, mask_=0, finished_=0, retiredCount_=0;
  uint64_t session_=0, retired_[8]={}, lastStatus_=0, lastRemote_=0, adoptedAt_=0,
      finishSeen_=0, elapsed_=0, revealLocal_=0, revealRemote_=0;
  bool bound_=false, online_=false, terminal_=false, scheduled_=false,
      haveResult_=false, peer_=false, locked_=false;
  Mode mode_=Mode::Flying;
  DisplayState state_=DisplayState::Offline;
  Timebase clock_;
};
}
