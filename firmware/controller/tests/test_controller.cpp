#include <cassert>
#include <cstdio>
#include "controller.hpp"
#include "beam.hpp"
#include "button.hpp"
#include "efforts.hpp"
#include "remotes.hpp"
#include <cstring>
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
  b[26]=128; assert(!decode(b,sizeof(b),q));
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
  assert(c.observe(finishMac,p,t+10)); assert(c.state==State::Running);
  c.tick(t+20,true); assert(c.state==State::Revealing && c.revealAt==t+20+kRevealLeadUs);
  c.tick(c.revealAt-1,true); assert(c.state==State::Revealing);
  c.tick(c.revealAt,true); assert(c.state==State::Complete);
  p.value=1; assert(!c.observe(finishMac,p,c.revealAt+1)); assert(c.results[0]==1750000);
  c.cancel(); assert(!c.localStart(t+1500000)); assert(!c.observe(finishMac,p,t+1500000));
  prepared(c,t+2000000); assert(c.attempt==2 && c.revealAt==0); p.attempt=1; assert(!c.observe(finishMac,p,t+2000010));
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
  assert(c.observe(finishMac,fn1,t+14)); assert(c.state==State::Running);
  c.tick(t+15,true); assert(c.state==State::Revealing && c.finished==3);
  c.tick(c.revealAt,true); assert(c.state==State::Complete);
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
void revealProtocol() {
  Packet p; p.kind=Kind::Reveal; p.system=42; p.session=99; p.attempt=3;
  p.boot=7; p.mask=15; p.finishedMask=5; p.value=12345678;
  uint8_t b[kPacketSize]; encode(p,b); Packet q;
  assert(decode(b,sizeof(b),q) && q.kind==Kind::Reveal && q.finishedMask==5 && q.value==p.value);
  p.finishedMask=0; encode(p,b); assert(decode(b,sizeof(b),q)); // All DNF is valid.
  p.finishedMask=15; encode(p,b); assert(decode(b,sizeof(b),q));
  b[27]=16; assert(!decode(b,sizeof(b),q));
  p.mask=3; p.finishedMask=4; encode(p,b); assert(!decode(b,sizeof(b),q));
  p.finishedMask=1; p.role=Role::Finish; encode(p,b); assert(!decode(b,sizeof(b),q));
  p.role=Role::Start; encode(p,b); assert(!decode(b,sizeof(b),q));
  p.role=Role::Controller; p.lane=2; encode(p,b); assert(!decode(b,sizeof(b),q));
  p.lane=1; p.value=0; encode(p,b); assert(!decode(b,sizeof(b),q));
  p.value=123; p.session=0; encode(p,b); assert(!decode(b,sizeof(b),q));
  p.session=99; p.attempt=0; encode(p,b); assert(!decode(b,sizeof(b),q));
  // Controller Status byte 27: remote-control holder 0-4 plus the ST-1 fault bit.
  p.attempt=3; p.kind=Kind::Status; encode(p,b); assert(decode(b,sizeof(b),q) && q.finishedMask==1);
  p.finishedMask=4|kControllerFault; encode(p,b); assert(decode(b,sizeof(b),q));
  p.finishedMask=5; encode(p,b); assert(!decode(b,sizeof(b),q));
  p.finishedMask=8; encode(p,b); assert(!decode(b,sizeof(b),q));
  p.finishedMask=1; p.role=Role::Finish; p.lane=1; encode(p,b); assert(!decode(b,sizeof(b),q));  // Peers: zero.
  p.role=Role::Controller; p.finishedMask=0; encode(p,b); assert(decode(b,sizeof(b),q) && q.finishedMask==0);
  b[5]=9; assert(!decode(b,sizeof(b),q));
}
void manualReveal() {
  Controller c(42,99); c.lanes=2;
  const uint8_t stMac[]={2,0,0,0,2,2}, fnMac[]={2,0,0,0,3,2};
  auto f1=status(), f2=status(Role::Finish,2), st=status(Role::Start,2);
  f1.mask=f2.mask=st.mask=3;
  c.observe(finishMac,f1,t); c.observe(fnMac,f2,t); c.observe(stMac,st,t);
  assert(!c.reveal(t)); assert(c.arm(t,true)); assert(!c.reveal(t+1));
  f1.attempt=f2.attempt=st.attempt=c.attempt; f1.flags=f2.flags=st.flags=jns::Armed;
  c.observe(finishMac,f1,t+1); c.observe(fnMac,f2,t+1); c.observe(stMac,st,t+1);
  c.tick(t+2,true); assert(!c.reveal(t+2)); assert(c.localStart(t+3));
  st.kind=Kind::Start; assert(c.observe(stMac,st,t+4));
  f1.kind=Kind::Result; f1.flags=jns::Complete; f1.value=1750000;
  assert(c.observe(finishMac,f1,t+5));
  assert(c.reveal(t+6) && c.finished==1 && c.active());
  const auto scheduled=c.revealAt;
  assert(!c.reveal(t+7) && c.revealAt==scheduled);
  assert(!c.arm(t+8,true)); assert(!c.countdown(t+8));
  f2.kind=Kind::Result; f2.flags=jns::Complete; f2.value=1800000;
  assert(!c.observe(fnMac,f2,t+9)); // Lane 2 stays DNF even if its result arrives now.
  assert(c.finished==1 && c.results[1]==0);
  c.tick(scheduled-1,true); assert(c.state==State::Revealing);
  c.tick(scheduled,true); assert(c.state==State::Complete);
  assert(!c.arm(scheduled+1,true)); // Failed rearm must retain the displayed result/mask.
  assert(c.finished==1 && c.results[0]==1750000);
  c.cancel(); assert(c.revealAt==0 && c.finished==0 && c.state==State::Idle);
  assert(!c.observe(fnMac,f2,scheduled+2));
}
void revealBoundaries() {
  { Controller c(42,99); prepared(c); assert(c.localStart(t+3));
    assert(c.reveal(t+4) && c.finished==0); // Coach can end a run with no finishes.
    c.cancel(); c.tick(t+2000000,true); assert(c.state==State::Idle && !c.revealAt); }
  { Controller c(42,99); prepared(c); assert(c.localStart(t+3)); assert(c.reveal(t+4));
    auto p=status(); p.boot=8; c.observe(finishMac,p,t+5);
    assert(c.state==State::Fault && !c.revealAt); }
  { Controller c(42,99); prepared(c); assert(c.localStart(t+3));
    assert(!c.reveal(t+kAttemptUs+1) && c.state==State::Fault); }
  { Controller c(42,99); prepared(c); assert(c.localStart(t+3));
    assert(c.reveal(t+kAttemptUs)); // Accepted before deadline; lead-in may extend beyond it.
    auto p=status(); p.attempt=c.attempt; p.flags=jns::Armed;
    c.observe(finishMac,p,c.revealAt-1);
    c.tick(c.revealAt,true); assert(c.state==State::Complete); }
  { Controller c(42,99); prepared(c); assert(c.localStart(t+3));
    auto p=status(); p.kind=Kind::Result; p.attempt=c.attempt; p.flags=jns::Complete; p.value=100;
    assert(c.observe(finishMac,p,t+4));
    c.tick(t+1000000,true); assert(c.revealAt==t+2000000); // Full lead-in after queue delay.
    c.fail("test fault"); assert(!c.revealAt && !c.reveal(t+1000001)); }
  { Controller c(42,99); c.mode=Mode::Standing; prepared(c); assert(c.countdown(t+3));
    assert(!c.reveal(t+4)); }
}
void revealFaults() {
  { Controller c(42,99); prepared(c); c.localStart(t+3); c.reveal(t+4);
    auto p=status(); p.attempt=c.attempt; p.flags=jns::Armed|jns::Fault;
    c.observe(finishMac,p,t+5); c.tick(t+6,true);
    assert(c.state==State::Fault && !c.revealAt); }
  { Controller c(42,99); prepared(c); c.localStart(t+3); c.reveal(t+4);
    c.tick(t+kPeerUs+2,true); assert(c.state==State::Fault && !c.revealAt); }
  { Controller c(42,99); prepared(c); c.localStart(t+3); c.reveal(t+4);
    c.tick(c.revealAt,true); assert(c.state==State::Complete);
    auto p=status(); c.observe(finishMac,p,t+2000000);
    assert(!c.arm(t+2000001,false) && c.state==State::Complete); }
}
void holdGesture() {
  Button b;
  assert(!b.update(true,0,true)); assert(b.update(true,30000,true));
  assert(!b.held(1029999)); assert(b.held(1030000)); assert(!b.held(2030000));
  b.update(false,2100000,true); b.update(false,2130000,true);
  b.update(true,2200000,true); assert(b.update(true,2230000,true));
  b.update(false,2300000,true); assert(!b.held(3300000)); // Short press/release never reveals.
  b.update(false,3330000,true);
  b.update(true,3400000,false); assert(b.update(true,3430000,false)); // ARM/GO press.
  b.update(true,8000000,true); assert(!b.held(9000000)); // Cannot carry hold into Running.
  b.update(false,10000000,true); b.update(false,10030000,true);
  b.update(true,10100000,true); b.update(true,10130000,true);
  b.update(true,10200000,false); b.update(true,10300000,true);
  assert(!b.held(12000000)); // Leaving Running disarms the gesture until another press.
}
void retirementProtocol() {
  Packet p; p.boot=7; p.kind=Kind::Status; p.flags=jns::Retired;
  uint8_t b[kPacketSize]; Packet out; encode(p,b); assert(decode(b,sizeof(b),out));
  b[4]=2; assert(!decode(b,sizeof(b),out)); // Upgrade the entire installation.
  p.flags|=jns::Armed; encode(p,b); assert(!decode(b,sizeof(b),out));
  p.flags=jns::Retired; p.role=Role::Finish; encode(p,b); assert(!decode(b,sizeof(b),out));
  p.role=Role::Controller; p.kind=Kind::Arm; encode(p,b); assert(!decode(b,sizeof(b),out));
}

void tapAndHold() {
  Button b;  // MODE: a tap acts on release; a hold fires once and suppresses the tap.
  b.update(true,0,true); assert(b.update(true,30000,true));
  b.update(false,100000,true); assert(!b.tapped()); b.update(false,130000,true); assert(b.tapped());
  b.update(false,140000,true); assert(!b.tapped());  // Reported once.
  b.update(true,200000,true); b.update(true,230000,true);
  assert(!b.held(1229999)); assert(b.held(1230000));
  b.update(false,1300000,true); b.update(false,1330000,true); assert(!b.tapped());
  Button c;  // CANCEL: a two-second hold.
  c.update(true,0,true); c.update(true,30000,true);
  assert(!c.held(1030000,2000000)); assert(c.held(2030000,2000000)); assert(!c.held(3000000,2000000));
  Button d;  // Pressed during an attempt and released after it: neither a hold nor a tap.
  d.update(true,0,false); d.update(true,30000,false); assert(!d.held(5000000,2000000));
  d.update(false,5100000,true); d.update(false,5130000,true); assert(!d.tapped());
}
Closed finishedRun(Controller& c,uint64_t us) {
  prepared(c); c.localStart(t+5);
  auto p=status(); p.kind=Kind::Result; p.attempt=c.attempt; p.flags=jns::Complete; p.value=us;
  assert(c.observe(finishMac,p,t+10)); c.tick(t+20,true); assert(c.state==State::Revealing);
  assert(!c.closed.pending);  // Not until the reveal: CANCEL in the lead-in still discards it.
  c.tick(c.revealAt,true); assert(c.state==State::Complete && c.closed.pending);
  return c.closed;
}
void closedAttempts() {
  { Controller c(42,99); Closed x=finishedRun(c,1750400);
    assert(x.ending==Ending::Revealed && x.attempt==1 && x.mask==1 && x.finished==1 && x.results[0]==1750400);
    c.closed.pending=false; c.cancel(); assert(!c.closed.pending); }  // Clearing a result is not a new effort.
  { Controller c(42,99); prepared(c); c.cancel(); assert(!c.closed.pending); }  // Nobody started.
  { Controller c(42,99); c.mode=Mode::Standing; prepared(c); c.countdown(t+100); c.cancel();
    assert(!c.closed.pending); }  // Cancelled before GO.
  { Controller c(42,99); prepared(c); c.localStart(t+5); c.cancel();
    assert(c.closed.pending && c.closed.ending==Ending::Cancelled && !c.closed.finished); }
  { Controller c(42,99); prepared(c); c.localStart(t+5); c.tick(t+kAttemptUs+1,true);
    assert(c.state==State::Fault && c.closed.pending && c.closed.ending==Ending::Invalid); }
  { Controller c(42,99); prepared(c); c.localStart(t+5); c.reveal(t+6); c.cancel();
    assert(c.closed.ending==Ending::Cancelled); }  // Discarded during the reveal lead-in.
  { Controller c(42,99); prepared(c); c.localStart(t+5); c.reveal(t+6); c.tick(c.revealAt,true);
    assert(c.closed.ending==Ending::Revealed && !c.closed.finished); }  // Early reveal: DNF.
}
void effortQueue() {
  assert(wholeMs(1750400)==1750 && wholeMs(1750500)==1751 && wholeMs(200)==1);
  assert(tenthsKmh(30,2000)==540 && tenthsKmh(50,0)==0 && tenthsKmh(10,1234)==292);
  EffortQueue q; Closed c; c.attempt=42; c.mask=0x3; c.finished=0x1; c.results[0]=1405400;
  assert(q.add(c,0x1A2B3C4D,30) && q.size()==2 && q.pending()==2);
  assert(!strcmp(q.at(0).id,"1A2B3C4D-42-L1") && !strcmp(q.at(1).id,"1A2B3C4D-42-L2"));
  assert(q.at(0).outcome==Outcome::Completed && q.at(0).ms==1405 && q.at(0).distance==30);
  assert(q.at(1).outcome==Outcome::Dnf && q.at(1).ms==0);
  c.ending=Ending::Invalid; c.mask=1; c.attempt=43; assert(q.add(c,0x1A2B3C4D,30));
  assert(q.at(2).outcome==Outcome::Invalid && q.at(2).ms==0);
  c.ending=Ending::Cancelled; c.attempt=44; assert(q.add(c,1,30) && q.at(3).outcome==Outcome::Cancelled);
  char body[384];
  assert(effortJson(q.at(0),"7d0e8a8c-0000-4000-8000-000000000000","04A1B2C3D4E5F6",body,sizeof(body)));
  assert(!strcmp(body,"{\"version\":1,\"resultId\":\"1A2B3C4D-42-L1\",\"sessionId\":\"7d0e8a8c-0000-4000-8000-000000000000\","
    "\"lane\":1,\"uid\":\"04A1B2C3D4E5F6\",\"outcome\":\"completed\",\"elapsedMs\":1405}"));
  assert(effortJson(q.at(1),"s","u",body,sizeof(body)));
  assert(!strcmp(body,"{\"version\":1,\"resultId\":\"1A2B3C4D-42-L2\",\"sessionId\":\"s\",\"lane\":2,\"uid\":\"u\",\"outcome\":\"dnf\"}"));
  assert(!effortJson(q.at(0),"s","u",body,20));  // Never send a truncated body.
  q.at(0).upload=Upload::Saved; q.at(2).upload=Upload::Rejected; q.compact();
  assert(q.size()==2 && !strcmp(q.at(0).id,"1A2B3C4D-42-L2") && q.at(1).outcome==Outcome::Cancelled);
  // Full queue: an attempt is kept whole or not at all.
  EffortQueue full; Closed four; four.attempt=1; four.mask=0xF;
  for (int i=0;i<16;++i) { four.attempt=i+1; assert(full.add(four,1,10)); }
  assert(full.room()==0 && !full.add(four,1,10) && full.size()==64);
  Closed none; assert(!full.add(none,1,10));
}
void replies() {
  char code[28];
  assert(replyCode("{\"version\":1,\"code\":\"SAVED\",\"replay\":true}",code,sizeof(code)) && !strcmp(code,"SAVED"));
  assert(replyCode("{\"code\" : \"TAG_UNAVAILABLE\"}",code,sizeof(code)) && !strcmp(code,"TAG_UNAVAILABLE"));
  assert(!replyCode("<html>Bad gateway</html>",code,sizeof(code)));
  assert(!replyCode("{\"code\":\"UNTERMINATED",code,sizeof(code)));
  assert(!replyCode("{\"code\":\"A_VERY_LONG_CODE_THAT_DOES_NOT_FIT\"}",code,8));
  bool stop;
  assert(judge(200,"SAVED",stop)==Upload::Saved && !stop);
  assert(judge(422,"NOT_IN_SESSION",stop)==Upload::Rejected && !stop);
  assert(judge(409,"RESULT_ID_CONFLICT",stop)==Upload::Rejected && !stop);
  assert(judge(409,"CONTROLLER_LIMIT_REACHED",stop)==Upload::Pending && stop);
  assert(judge(503,"BACKEND_UNAVAILABLE",stop)==Upload::Pending && stop);
  assert(judge(401,"INVALID_CREDENTIAL",stop)==Upload::Pending && stop);
  assert(judge(-1,"NO_REPLY",stop)==Upload::Pending && stop);
  assert(judge(200,"HTTP_200",stop)==Upload::Pending && stop);
}

Packet remotePacket(uint32_t boot=77,uint64_t session=99,uint32_t attempt=3) {
  Packet p; p.kind=Kind::Command; p.role=Role::Remote; p.lane=1; p.system=42; p.session=session;
  p.attempt=attempt; p.boot=boot; return p;
}
Command press(Code code,uint32_t seq,uint16_t age=10) { Command c; c.code=code; c.seq=seq; c.arg=age; return c; }
void remoteControl() {
  const uint8_t a[6]={1,2,3,4,5,6}, b[6]={9,9,9,9,9,9}, stranger[6]={7,7,7,7,7,7};
  Remotes r; RemoteView v; v.session=99; v.attempt=3; v.armable=true;
  // Pairing: only within the window, first asker is the candidate, confirmed on ST-1.
  Command pair; pair.code=Code::Pair;
  RemoteOutcome o=r.handle(a,remotePacket(),pair,v,0); assert(!o.replies);
  uint8_t cand[6]; assert(!r.candidate(cand));
  r.openPairing(1000); r.handle(a,remotePacket(),pair,v,2000); r.handle(b,remotePacket(),pair,v,3000);
  assert(r.candidate(cand) && !memcmp(cand,a,6) && r.confirm()==1 && !r.pairing(4000));
  r.openPairing(5000); r.handle(b,remotePacket(),pair,v,6000); assert(r.confirm()==2);
  r.openPairing(7000); r.handle(a,remotePacket(),pair,v,8000); assert(r.confirm()==1 && r.count()==2);  // Re-pair keeps slot.
  // Unpaired senders are told so; nothing happens.
  o=r.handle(stranger,remotePacket(),press(Code::Arm,1),v,10000);
  assert(o.replies && o.reply==Reply::Refused && o.reason==Reason::NotPaired && o.action==RemoteAction::None);
  // Nobody holds control: ARM refused until control is taken.
  o=r.handle(a,remotePacket(),press(Code::Arm,1),v,11000); assert(o.reason==Reason::NotInControl && o.slot==1);
  o=r.handle(a,remotePacket(),press(Code::TakeControl,2),v,12000); assert(o.reply==Reply::Accepted && r.holder()==1);
  // Holder present: the other remote cannot take control, but its CANCEL always works.
  o=r.handle(b,remotePacket(88),press(Code::TakeControl,1),v,13000); assert(o.reason==Reason::NotInControl && r.holder()==1);
  o=r.handle(b,remotePacket(88),press(Code::Cancel,2,60000),v,14000);
  assert(o.reply==Reply::Accepted && o.action==RemoteAction::Cancel);  // Never too late to cancel.
  o=r.handle(b,remotePacket(88),press(Code::Arm,3),v,15000); assert(o.reason==Reason::NotInControl);
  // Holder's ARM is acted on once; a retry is answered again without acting.
  o=r.handle(a,remotePacket(),press(Code::Arm,3),v,16000); assert(o.reply==Reply::Accepted && o.action==RemoteAction::Arm);
  o=r.handle(a,remotePacket(),press(Code::Arm,3),v,16040); assert(o.reply==Reply::Accepted && o.action==RemoteAction::None);
  o=r.handle(a,remotePacket(),press(Code::Arm,2),v,16080); assert(o.reason==Reason::TooLate && o.action==RemoteAction::None);
  // An ARM that ST-1 then could not do: its retry is refused, not re-tried.
  o=r.handle(a,remotePacket(),press(Code::Arm,4),v,17000); assert(o.action==RemoteAction::Arm);
  r.amend(1,Reason::NotReady); o=r.handle(a,remotePacket(),press(Code::Arm,4),v,17040);
  assert(o.reply==Reply::Refused && o.reason==Reason::NotReady && o.action==RemoteAction::None);
  // Late, stale or not-ready presses are refused.
  o=r.handle(a,remotePacket(),press(Code::Go,5,kCommandMaxAgeMs+1),v,18000); assert(o.reason==Reason::TooLate);
  o=r.handle(a,remotePacket(),press(Code::Go,6,kCommandMaxAgeMs),v,18100); assert(o.reason==Reason::NotReady);  // Not armed.
  v.canGo=true; o=r.handle(a,remotePacket(),press(Code::Go,7),v,18200); assert(o.action==RemoteAction::Go);
  o=r.handle(a,remotePacket(77,99,2),press(Code::Go,8),v,18300); assert(o.reason==Reason::WrongAttempt);
  o=r.handle(a,remotePacket(77,55),press(Code::Go,9),v,18400); assert(o.reason==Reason::WrongSession);
  v.canReveal=true; o=r.handle(a,remotePacket(),press(Code::Reveal,10),v,18500); assert(o.action==RemoteAction::Reveal);
  v.armable=false; o=r.handle(a,remotePacket(),press(Code::Arm,11),v,18600); assert(o.reason==Reason::NotReady);
  // A remote restart starts a new sequence.
  o=r.handle(a,remotePacket(78),press(Code::Arm,1),v,18700); assert(o.reason==Reason::NotReady && o.replies);
  // Hello: battery and presence, no reply.
  Command hello; hello.code=Code::Hello; hello.arg=82;
  o=r.handle(a,remotePacket(78),hello,v,19000); assert(!o.replies && r.battery(1)==82 && r.present(1,19000));
  assert(!r.present(1,19000+Remotes::kPresentUs));
  // Never during an attempt; then, once the holder has been silent 30 s, another may take over.
  v.active=true; o=r.handle(b,remotePacket(88),press(Code::TakeControl,4),v,20000); assert(o.reason==Reason::Busy);
  v.active=false;
  o=r.handle(b,remotePacket(88),press(Code::TakeControl,5),v,19000+Remotes::kSilentUs-1); assert(o.reason==Reason::NotInControl);
  o=r.handle(b,remotePacket(88),press(Code::TakeControl,6),v,19000+Remotes::kSilentUs); assert(o.reply==Reply::Accepted && r.holder()==2);
  // ST-1's own CANCEL releases control; forgetting clears everything.
  r.release(); assert(!r.holder());
  uint8_t saved[24]; const uint8_t used=r.save(saved); assert(used==3);
  Remotes back; back.restore(saved,used); assert(back.slotOf(a)==1 && back.slotOf(b)==2 && !back.holder());
  back.forget(); assert(!back.count() && !back.slotOf(a));
  // Pairing a fifth remote fails cleanly.
  Remotes full; const uint8_t m[5][6]={{1},{2},{3},{4},{5}};
  for (int i=0;i<5;++i) { full.openPairing(0); full.handle(m[i],remotePacket(),pair,v,1); assert(full.confirm()==(i<4?i+1:0)); }
  // Command packing round-trips; nonsense is rejected.
  Command c; c.code=Code::Go; c.reply=Reply::Refused; c.reason=Reason::TooLate; c.arg=251; c.seq=0xABCDEF;
  Command d; assert(unpackCommand(packCommand(c),d) && d.code==Code::Go && d.reply==Reply::Refused &&
    d.reason==Reason::TooLate && d.arg==251 && d.seq==0xABCDEF);
  assert(!unpackCommand(0,d) && !unpackCommand(8,d) && !unpackCommand(1|(3ULL<<8),d));
  // Wire: remotes may only send Command; Command only from remote or controller.
  Packet w=remotePacket(); w.value=packCommand(press(Code::Arm,1)); uint8_t bytes[kPacketSize]; Packet q;
  encode(w,bytes); assert(decode(bytes,sizeof(bytes),q) && q.role==Role::Remote && q.kind==Kind::Command);
  w.kind=Kind::Status; encode(w,bytes); assert(!decode(bytes,sizeof(bytes),q));
  w.kind=Kind::Command; w.role=Role::Finish; encode(w,bytes); assert(!decode(bytes,sizeof(bytes),q));
  w.role=Role::Controller; encode(w,bytes); assert(decode(bytes,sizeof(bytes),q));
}
int main() {
  retirementProtocol();
  protocol(); flying(); standing(); faults(); multilane(); optics();
  revealProtocol(); manualReveal(); revealBoundaries(); revealFaults(); holdGesture();
  tapAndHold(); closedAttempts(); effortQueue(); replies(); remoteControl();
  puts("All controller tests passed");
}
