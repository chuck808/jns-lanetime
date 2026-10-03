#include <cassert>
#include <cstdio>
#include "controller.hpp"
#include "beam.hpp"
using namespace jns;
constexpr uint64_t t=4000000;
const uint8_t finishMac[]={2,0,0,0,3,1};
Packet status(Role role=Role::Finish,uint8_t lane=1) {
  Packet p; p.system=42; p.session=99; p.role=role; p.lane=lane; p.boot=7; p.flags=jns::Ready; return p;
}
void prepared(Controller& c,uint64_t at=t) {
  auto p=status(); p.mode=c.mode; p.mask=c.mask(); c.observe(finishMac,p,at);
  assert(c.arm(at,true)); p.attempt=c.attempt; p.flags=jns::Ready|jns::Armed;
  c.observe(finishMac,p,at+1); c.tick(at+2,true); assert(c.state==State::Armed);
}
void protocol() {
  Packet p=status(); p.kind=Kind::Result; p.flags=jns::Complete;
  p.session=0x0102030405060708ULL; p.attempt=0x11223344; p.value=1750123;
  uint8_t b[kPacketSize]; encode(p,b);
  assert(b[12]==8 && b[19]==1 && b[20]==0x44 && b[23]==0x11);
  Packet q; assert(decode(b,sizeof(b),q)); assert(q.value==1750123 && q.session==p.session && q.boot==p.boot);
  for (size_t i=0;i<kPacketSize;++i) assert(!decode(b,i,q));
  b[27]=1; assert(!decode(b,sizeof(b),q)); b[27]=0;
  b[4]=1; assert(!decode(b,sizeof(b),q)); b[4]=kVersion; // v1 retired.
  b[7]=5; assert(!decode(b,sizeof(b),q)); b[7]=1;
  b[5]=uint8_t(Kind::Go); assert(!decode(b,sizeof(b),q));
  b[5]=uint8_t(Kind::Result); b[26]=jns::Complete|jns::Cued; assert(decode(b,sizeof(b),q));
  b[26]=64; assert(!decode(b,sizeof(b),q));
}
void flying() {
  Controller c(42,99); assert(!c.arm(t,true));
  auto p=status(); c.observe(finishMac,p,t); assert(!c.arm(t,false)); prepared(c);
  assert(c.localStart(t+5)); assert(!c.localStart(t+6)); assert(c.startAt==t+5);
  assert(c.startPending(t+7)); // FN-1 has not yet reported Running.
  { auto r=p; r.attempt=c.attempt; r.flags=jns::Armed|jns::Running; c.observe(finishMac,r,t+8); }
  assert(!c.startPending(t+9));
  p.kind=Kind::Result; p.attempt=c.attempt; p.flags=jns::Complete; p.value=1750000;
  auto wrong=p; wrong.session=98; assert(!c.observe(finishMac,wrong,t+10));
  wrong=p; wrong.attempt=0; assert(!c.observe(finishMac,wrong,t+10));
  wrong=p; wrong.boot=8; assert(!c.observe(finishMac,wrong,t+10));
  wrong=p; wrong.flags|=jns::Fault; assert(!c.observe(finishMac,wrong,t+10));
  uint8_t alien[]={2,0,0,0,3,9}; assert(!c.observe(alien,p,t+10));
  assert(c.observe(finishMac,p,t+10)); assert(c.state==State::Complete);
  p.value=1; assert(!c.observe(finishMac,p,t+11)); assert(c.results[0]==1750000);
  c.cancel(); assert(!c.localStart(t+12)); assert(!c.observe(finishMac,p,t+12));
  prepared(c,t+100); assert(c.attempt==2); p.attempt=1; assert(!c.observe(finishMac,p,t+110));
}
void standing() {
  Controller c(42,99); c.mode=Mode::Standing; prepared(c);
  assert(!c.localStart(t+50)); assert(c.countdown(t+100)); assert(!c.countdown(t+101));
  const uint64_t go=t+100+kCueLeadUs+kCueBeeps*kCueStepUs;
  assert(c.goAt==go && c.firstCue()==t+100+kCueLeadUs);
  auto p=status(); p.mode=c.mode; p.mask=c.mask(); p.attempt=c.attempt; p.flags=jns::Ready|jns::Armed;
  c.tick(t+200,true); assert(c.state==State::Countdown); // Not yet cued; still in lead-in.
  p.flags|=jns::Cued; p.value=go-1; c.observe(finishMac,p,t+300);
  assert(!c.cued(t+300)); // Wrong GO time is not an acknowledgement.
  p.value=go; c.observe(finishMac,p,t+400); assert(c.cued(t+400));
  c.tick(c.firstCue(),true); assert(c.state==State::Countdown);
  c.observe(finishMac,p,go-1000); c.tick(go-1,true); assert(c.state==State::Countdown);
  c.tick(go,true); assert(c.state==State::Running && c.started==1);
  c.cancel(); assert(c.goAt==0 && !c.countdown(go+1));
  // A peer that never confirms the schedule stops the countdown before the first beep.
  Controller d(42,99); d.mode=Mode::Standing; prepared(d); assert(d.countdown(t+100));
  d.tick(d.firstCue(),true); assert(d.state==State::Fault);
  // Start sensors carry the cues, so Standing requires ST-2 when two lanes are enabled.
  Controller e(42,99); e.mode=Mode::Standing; e.lanes=2;
  const uint8_t finish2[]={2,0,0,0,3,2};
  auto f1=status(); f1.mode=e.mode; f1.mask=3; auto f2=status(Role::Finish,2); f2.mode=e.mode; f2.mask=3;
  e.observe(finishMac,f1,t); e.observe(finish2,f2,t); assert(!e.arm(t,true));
}
void faults() {
  { Controller c(42,99); auto p=status(); c.observe(finishMac,p,t); assert(c.arm(t+10,true));
    c.observe(finishMac,p,t+5); assert(c.state==State::Preparing); } // Queued pre-arm announcement.
  { Controller c(42,99); prepared(c); c.tick(t+kPeerUs+2,true); assert(c.state==State::Fault); }
  { Controller c(42,99); prepared(c); auto p=status(); p.boot=8; c.observe(finishMac,p,t+5); assert(c.state==State::Fault); }
  { Controller c(42,99); auto p=status(); c.observe(finishMac,p,t); assert(c.arm(t,true));
    c.tick(t+kPeerUs+1,true); assert(c.state==State::Fault); }
  { Controller c(42,99); auto p=status(); c.observe(finishMac,p,t); assert(c.arm(t,true));
    c.tick(t+10,false); assert(c.state==State::Fault); }
  { Controller c(42,99); prepared(c); auto p=status(); p.attempt=c.attempt; p.flags=jns::Armed;
    uint8_t duplicate[]={2,0,0,0,3,2}; c.observe(duplicate,p,t+10); assert(c.state==State::Fault); }
  { Controller c(42,99); auto p=status(Role::Controller); c.observe(finishMac,p,t); assert(!c.arm(t,true)); }
  { Controller c(42,99); auto p=status(Role::Start); c.observe(finishMac,p,t); assert(!c.arm(t,true)); }
  { Controller c(42,99); prepared(c); c.localStart(t); auto p=status(); p.kind=Kind::Result;
    p.attempt=c.attempt; p.flags=jns::Complete; p.value=100;
    assert(!c.observe(finishMac,p,t+kAttemptUs+1)); assert(c.state==State::Fault); }
}
void multilane() {
  Controller c(42,99); c.lanes=2;
  const uint8_t start2[]={2,0,0,0,2,2}, finish2[]={2,0,0,0,3,2};
  auto fn1=status(); fn1.mask=3; auto st2=status(Role::Start,2); st2.mask=3;
  auto fn2=status(Role::Finish,2); fn2.mask=3;
  c.observe(finishMac,fn1,t); c.observe(start2,st2,t); assert(!c.arm(t,true));
  c.observe(finish2,fn2,t); assert(c.arm(t,true));
  fn1.attempt=st2.attempt=fn2.attempt=c.attempt; fn1.flags=st2.flags=fn2.flags=jns::Armed;
  c.observe(finishMac,fn1,t+1); c.observe(start2,st2,t+1); c.observe(finish2,fn2,t+1);
  c.tick(t+2,true); assert(c.state==State::Armed);
  fn2.kind=Kind::Result; fn2.flags=jns::Complete; fn2.value=1200000;
  assert(!c.observe(finish2,fn2,t+10)); // Finish before start.
  st2.kind=Kind::Start; assert(c.observe(start2,st2,t+11));
  assert(c.observe(start2,st2,t+12)); assert(c.started==2); // Duplicate cannot restart a clock.
  assert(c.observe(finish2,fn2,t+13)); assert(c.state==State::Running);
  assert(c.localStart(t+14)); fn1.kind=Kind::Result; fn1.flags=jns::Complete; fn1.value=1800000;
  assert(c.observe(finishMac,fn1,t+14)); assert(c.state==State::Complete);
}
void optics() {
  Beam b; assert(!b.clear(100000)); b.edge(false,100000);
  assert(!b.clear(119999)); assert(b.clear(120000));
  uint64_t at;
  b.edge(true,130000); b.edge(false,130100); assert(!b.take(at)); // Short glitch.
  b.edge(true,150000); b.edge(false,150300); // Entire break between loop iterations.
  assert(b.take(at) && at==150000); assert(!b.take(at));
  assert(!b.clear(160000)); assert(b.clear(180000));
  b.edge(true,200000); b.advance(200200); assert(b.take(at) && at==200000);
  b.advance(300000); assert(!b.take(at));
}
int main() { protocol(); flying(); standing(); faults(); multilane(); optics(); puts("All controller tests passed"); }
