#include <cassert>
#include <cstdio>
#include "remote_logic.hpp"
using namespace jns;
Mirror heard(uint8_t flags,uint8_t control,Mode mode=Mode::Standing,uint64_t at=1000000) {
  Packet p; p.kind=Kind::Status; p.role=Role::Controller; p.session=9; p.attempt=4; p.mode=mode;
  p.flags=flags; p.finishedMask=control; Mirror m; m.take(p,at); return m;
}
void lights() {
  Mirror none; assert(glowFor(none,1,0)==Glow::Searching);
  assert(glowFor(heard(Ready,1),1,1000000+1999999)==Glow::Ready);
  assert(glowFor(heard(Ready,1),1,1000000+2000000)==Glow::Searching);  // Stale: never show old state.
  assert(glowFor(heard(Ready,2),1,1000000)==Glow::NotInControl);
  assert(glowFor(heard(Ready,0),1,1000000)==Glow::NotInControl);       // Nobody: take control first.
  assert(glowFor(heard(0,1),1,1000000)==Glow::NotReady);
  assert(glowFor(heard(Armed,1),1,1000000)==Glow::Armed);
  assert(glowFor(heard(Armed|Running,1),1,1000000)==Glow::Running);
  assert(glowFor(heard(Complete,1),1,1000000)==Glow::Revealed);
  assert(glowFor(heard(Ready,1|kControllerFault),1,1000000)==Glow::Fault);
  assert(glowFor(heard(Ready,2|kControllerFault),1,1000000)==Glow::Fault);  // Fault shows to everyone.
  const Rgb a=colourFor(Glow::Searching,0), b=colourFor(Glow::Searching,600000); assert(a.r!=b.r);  // Pulses.
}
void buttonsMeanings() {
  const uint64_t t=1000000;
  assert(pressCode(heard(Ready,1),1,t)==uint8_t(Code::Arm));
  assert(pressCode(heard(0,1),1,t)==uint8_t(Code::Arm));      // ST-1 decides; it may refuse NotReady.
  assert(pressCode(heard(Complete,1),1,t)==uint8_t(Code::Arm)); // Next rep.
  assert(pressCode(heard(Armed,1,Mode::Standing),1,t)==uint8_t(Code::Go));
  assert(pressCode(heard(Armed,1,Mode::Flying),1,t)==0);       // Flying: the rider starts it.
  assert(pressCode(heard(Armed|Running,1),1,t)==0);
  assert(pressCode(heard(Ready,2),1,t)==0);                    // Not in control.
  assert(pressCode(heard(Ready,1|kControllerFault),1,t)==0);
  assert(pressCode(Mirror(),1,t)==0);
  assert(holdCode(heard(Armed|Running,1),1,t,999999)==0);
  assert(holdCode(heard(Armed|Running,1),1,t,1000000)==uint8_t(Code::Reveal));
  assert(holdCode(heard(Armed|Running,2),1,t,5000000)==0);      // Only the holder reveals.
  assert(holdCode(heard(Ready,2),1,t,1999999)==0);
  assert(holdCode(heard(Ready,2),1,t,2000000)==uint8_t(Code::TakeControl));
  assert(holdCode(heard(Ready,0),1,t,2000000)==uint8_t(Code::TakeControl));
  assert(holdCode(heard(Armed,2),1,t,9000000)==0);              // Never mid-attempt.
  assert(holdCode(heard(Ready,1),1,t,9000000)==0);              // Already in control.
}
void retries() {
  Pending p; assert(!p.due(0));
  p.start(Code::Go,7,1000000);
  assert(p.due(1000000) && !p.due(1039999) && p.due(1040000));
  assert(p.ageMs(1040000)==40);
  Command reply; reply.code=Code::Go; reply.seq=6; reply.reply=Reply::Accepted;
  assert(!p.answer(reply)); reply.seq=7; reply.reply=Reply::Request; assert(!p.answer(reply));
  reply.reply=Reply::Refused; assert(p.answer(reply) && !p.active());
  p.start(Code::Go,8,1000000);
  assert(p.due(1000000+250000));   // Still within ST-1's limit.
  assert(!p.due(1000000+250001+40000) && !p.active() && p.takeExpired() && !p.takeExpired());
  p.start(Code::Cancel,9,0); assert(p.due(999999) && !p.due(1000001+40000) && p.takeExpired());
  p.start(Code::Arm,0x1FFFFFF,0); assert(p.seq()==0xFFFFFF);
}
void button() {
  Button b; assert(!b.update(true,0)); assert(b.update(true,30000) && b.down());
  assert(b.heldFor(1030000)==1000000 && b.fireOnce() && !b.fireOnce());
  b.update(false,1100000); b.update(false,1130000); assert(!b.down());
  // The waking press is ignored until released; the next press counts.
  Button w; w.block(); assert(!w.update(true,0) && !w.update(true,30000) && !w.down() && !w.heldFor(5000000));
  w.update(false,100000); w.update(false,130000);
  w.update(true,200000); assert(w.update(true,230000) || w.down());
  assert(batteryPercent(3300)==0 && batteryPercent(4150)==100 && batteryPercent(3725)==50 && batteryPercent(5000)==100);
}
int main() { lights(); buttonsMeanings(); retries(); button(); puts("All remote tests passed"); }
