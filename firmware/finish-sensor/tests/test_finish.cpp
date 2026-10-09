#include <cassert>
#include <cstdio>
#include "finish_sensor.hpp"
#include "controller.hpp"
#include "lane_display.hpp"
using namespace jns;
const uint8_t controlMac[]={2,0,0,0,1,1},finishMac[]={2,0,0,0,3,1},alien[]={2,0,0,0,9,9};
constexpr uint64_t offset=1000000;
struct Fixture {
  FinishSensor f{42,9}; Packet c; uint64_t t=10000000;
  Fixture() { c.system=42; c.session=99; c.boot=7; f.edge(false,t+offset); }
  void beat() { c.kind=Kind::Status; c.value=t; f.receive(controlMac,c,t+offset); f.tick(t+offset); }
  void warm() { for (int i=0;i<20;++i) { beat(); t+=500000; } assert(f.ready(t+offset)); }
  void advance() { t+=500000; beat(); }
  void until(uint64_t at) { while (t+500000<=at) advance(); t=at; }
  void arm(Mode mode=Mode::Flying,uint32_t attempt=1) {
    c.mode=mode; c.attempt=attempt; c.kind=Kind::Arm; c.value=0;
    f.receive(controlMac,c,t+offset); assert(f.state()==FinishState::Armed);
    c.flags=jns::Armed;
  }
  void start(uint64_t at=0) {
    c.kind=c.mode==Mode::Flying?Kind::Start:Kind::Go; c.value=at?at:t;
    f.receive(controlMac,c,t+offset);
  }
  void pulse(uint64_t at,uint64_t duration=300) { until(at); f.edge(true,at+offset); f.edge(false,at+offset+duration); t=at+duration; }
  Packet result() { Packet p; assert(f.result(t+offset,p)); return p; }
};
void flyingAndLateStart() {
  for (bool late:{false,true}) {
    Fixture x; x.warm(); x.arm(); const uint64_t start=x.t+100000;
    if (!late) { x.t=start; x.start(); }
    x.pulse(start+1750000);
    if (late) { x.until(start+2000000); x.start(start); }
    while (x.t<start+5000000) x.advance();
    auto p=x.result(); assert(p.value==1750000 && p.role==Role::Finish && p.lane==1);
    assert((p.flags&(jns::Armed|jns::Complete))==(jns::Armed|jns::Complete));
    auto duplicate=x.c; duplicate.kind=Kind::Start; duplicate.value=start+50000;
    x.f.receive(controlMac,duplicate,x.t+offset); x.pulse(x.t+100000); x.advance();
    assert(x.result().value==1750000);
    auto arm=x.c; arm.kind=Kind::Arm; x.f.receive(controlMac,arm,x.t+offset);
    assert(x.result().value==1750000);
  }
}
void standingAndStrays() {
  Fixture x; x.warm(); x.arm(Mode::Standing); const uint64_t go=x.t+4000000;
  x.start(go); assert(x.f.state()==FinishState::Cued);
  auto status=x.f.status(x.t+offset); assert((status.flags&jns::Cued) && status.value==go && !(status.flags&jns::Running));
  x.pulse(x.t+500000); // Stray break in countdown.
  while (x.t<go) x.advance();
  assert(x.f.state()==FinishState::Running);
  x.pulse(go+2000000); while (x.t<go+5000000) x.advance();
  assert(x.result().value==2000000 && x.result().flags&jns::Cued);
}
void invalidationAndRecovery() {
  { Fixture x; x.warm(); x.arm(); x.start(); x.c.kind=Kind::Cancel; x.f.receive(controlMac,x.c,x.t+offset);
    x.c.kind=Kind::Arm; x.f.receive(controlMac,x.c,x.t+offset); assert(x.f.state()==FinishState::Idle);
    x.pulse(x.t+100000); x.advance(); Packet p; assert(!x.f.result(x.t+offset,p));
    x.arm(Mode::Flying,2); }
  { Fixture x; x.warm(); x.arm(); x.start(); x.f.tick(x.t+offset+1600000);
    assert(x.f.state()==FinishState::Invalid && x.f.status(x.t+offset).flags&jns::Fault); }
  { Fixture x; x.warm(); x.arm(); x.start(); x.t+=4000000; x.beat();
    assert(x.f.state()==FinishState::Invalid); }
  { Fixture x; x.warm(); x.arm(); for (int i=0;i<61;++i) x.advance();
    assert(x.f.state()==FinishState::Invalid); }
  { Fixture x; x.warm(); x.arm(); for (int i=0;i<9;++i) x.pulse(x.t+100000+i*100000);
    assert(x.f.state()==FinishState::Invalid); }
  { Fixture x; x.warm(); x.arm(); x.f.overflow(); x.c.kind=Kind::Cancel;
    x.f.receive(controlMac,x.c,x.t+offset); assert(!x.f.ready(x.t+offset)); }
  { Fixture x; x.warm(); x.c.attempt=3; x.c.kind=Kind::Cancel; x.f.receive(controlMac,x.c,x.t+offset);
    x.c.kind=Kind::Arm; x.f.receive(controlMac,x.c,x.t+offset); assert(x.f.state()==FinishState::Idle); }
}
void filtersAndReboots() {
  { Fixture x; x.warm(); x.arm(); auto p=x.c; p.kind=Kind::Start; p.value=x.t;
    p.system=43; x.f.receive(controlMac,p,x.t+offset); p.system=42;
    p.attempt=2; x.f.receive(controlMac,p,x.t+offset); p.attempt=1;
    p.session=98; x.f.receive(controlMac,p,x.t+offset); p.session=99;
    x.f.receive(alien,p,x.t+offset); assert(x.f.state()==FinishState::Armed);
    x.start(); assert(x.f.state()==FinishState::Running); }
  { Fixture x; x.warm(); x.arm(); x.start(); auto old=x.c;
    x.c.session=100; x.c.boot=8; x.c.attempt=0; x.c.flags=0; x.beat();
    assert(x.f.state()==FinishState::Idle && !x.f.ready(x.t+offset));
    old.kind=Kind::Status; old.value=x.t+100; x.f.receive(controlMac,old,x.t+offset+100);
    assert(x.f.status(x.t+offset).session==100); }
  { Fixture x; x.c.attempt=5; x.c.flags=jns::Running; x.warm();
    x.c.kind=Kind::Arm; x.f.receive(controlMac,x.c,x.t+offset);
    assert(x.f.state()==FinishState::Idle); x.arm(Mode::Flying,6); }
  { Fixture x; x.warm(); x.c.kind=Kind::Status; x.f.receive(alien,x.c,x.t+offset);
    assert(!x.f.ready(x.t+offset)); }
  { Fixture x; x.warm(); auto p=x.f.status(x.t+offset); x.f.receive(alien,p,x.t+offset);
    assert(!x.f.ready(x.t+offset)); }
}
void beamAndReveal() {
  { Fixture x; x.warm(); x.arm(); x.start(); x.pulse(x.t+100000,100);
    for (int i=0;i<6;++i) x.advance();
    assert(x.f.state()==FinishState::Running); }
  { Fixture x; x.warm(); x.arm(); x.start(); x.c.kind=Kind::Reveal; x.c.value=x.t+1000000;
    x.c.finishedMask=0; x.f.receive(controlMac,x.c,x.t+offset);
    assert(x.f.state()==FinishState::Stopped);
    auto p=x.f.status(x.t+offset); assert((p.flags&jns::Armed) && !(p.flags&jns::Fault));
    x.pulse(x.t+100000); for (int i=0;i<65;++i) x.advance();
    assert(!x.f.result(x.t+offset,p)); }
  { Fixture x; x.warm(); x.f.edge(true,x.t+offset); x.c.kind=Kind::Arm; x.c.attempt=1;
    x.f.receive(controlMac,x.c,x.t+offset); assert(x.f.state()==FinishState::Invalid); }
}
void endToEnd() {
  Controller controller(42,99); FinishSensor finish(42,9); LaneDisplay display(42,1);
  uint64_t t=10000000; finish.edge(false,t+offset);
  auto wire=[](const Packet& p) { uint8_t bytes[kPacketSize]; encode(p,bytes); Packet out; assert(decode(bytes,sizeof(bytes),out)); return out; };
  auto controllerPacket=[&](Kind kind) {
    Packet p; p.kind=kind; p.system=42; p.session=99; p.boot=7; p.attempt=controller.attempt;
    p.mode=controller.mode; p.mask=controller.mask();
    if (controller.active()) p.flags|=jns::Armed;
    if (controller.state==State::Running) p.flags|=jns::Running;
    if (controller.state==State::Complete || controller.state==State::Revealing) p.flags|=jns::Complete;
    if (kind==Kind::Status) p.value=t;
    return p;
  };
  auto heartbeat=[&]() {
    const auto p=wire(controllerPacket(Kind::Status)); finish.receive(controlMac,p,t+offset); display.receive(controlMac,p,t+offset);
    finish.tick(t+offset); const auto status=wire(finish.status(t+offset));
    controller.observe(finishMac,status,t); display.receive(finishMac,status,t+offset);
  };
  for (int i=0;i<20;++i,t+=500000) heartbeat();
  assert(controller.arm(t,true)); auto arm=wire(controllerPacket(Kind::Arm));
  finish.receive(controlMac,arm,t+offset); display.receive(controlMac,arm,t+offset);
  heartbeat(); controller.tick(t,true); assert(controller.state==State::Armed);
  t+=100000; assert(controller.localStart(t)); auto start=controllerPacket(Kind::Start); start.value=t;
  finish.receive(controlMac,wire(start),t+offset);
  const uint64_t finishAt=t+1750000;
  bool captured=false,delivered=false;
  for (int i=0;i<14;++i) {
    t+=500000;
    if (!captured && t>=finishAt+300) { finish.edge(true,finishAt+offset); finish.edge(false,finishAt+offset+300); captured=true; }
    heartbeat(); Packet result;
    if (finish.result(t+offset,result)) {
      result=wire(result); controller.observe(finishMac,result,t); display.receive(finishMac,result,t+offset); delivered=true;
    }
    controller.tick(t,true);
    if (controller.state==State::Revealing) {
      auto reveal=controllerPacket(Kind::Reveal); reveal.value=controller.revealAt; reveal.finishedMask=controller.finished;
      reveal=wire(reveal); finish.receive(controlMac,reveal,t+offset); display.receive(controlMac,reveal,t+offset);
    }
    display.tick(t+offset);
  }
  assert(delivered && controller.state==State::Complete && controller.results[0]==1750000);
  assert(display.state()==DisplayState::Time && display.elapsed()==1750000);
}
int main() {
  flyingAndLateStart(); standingAndStrays(); invalidationAndRecovery(); filtersAndReboots();
  beamAndReveal(); endToEnd(); puts("All FN-1 tests passed");
}
