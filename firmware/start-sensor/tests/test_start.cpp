#include <cassert>
#include <cstdio>
#include "start_sensor.hpp"
#include "cue_player.hpp"
#include "controller.hpp"
using namespace jns;
const uint8_t controllerMac[]={2,0,0,0,1,1},finishMac[]={2,0,0,0,3,2},alien[]={2,0,0,0,9,9};
constexpr uint64_t offset=1000000;
Packet wire(Packet p) { uint8_t b[kPacketSize]; encode(p,b); Packet out; assert(decode(b,sizeof(b),out)); return out; }
struct Fixture {
  StartSensor s; Packet c; uint64_t t=10000000;
  explicit Fixture(uint8_t lane=2):s(42,9,lane) { c.system=42; c.session=99; c.boot=7; c.mask=15; s.edge(false,t+offset); }
  void send(Packet p,const uint8_t* mac=controllerMac) { s.receive(mac,wire(p),t+offset); }
  void beat() { c.kind=Kind::Status; c.value=t; send(c); s.tick(t+offset); }
  void advance() { t+=500000; beat(); }
  void warm() { for (int i=0;i<20;++i) { beat(); t+=500000; } assert(s.ready(t+offset)); }
  void arm(Mode mode=Mode::Flying,uint32_t attempt=1) {
    c.mode=mode; c.attempt=attempt; c.kind=Kind::Arm; c.value=0; send(c);
    assert(s.state()==StartState::Armed); c.flags=jns::Armed;
  }
  void pulse(uint64_t duration=300) { s.edge(true,t+offset); s.edge(false,t+offset+duration); }
  Packet start() { Packet p; assert(s.start(t+offset,p)); return wire(p); }
  Packet finish() { auto p=s.status(t+offset); p.role=Role::Finish; p.boot=30; p.flags=jns::Armed; return p; }
};
void flying() {
  for (uint8_t lane=2;lane<=4;++lane) {
    Fixture x(lane); x.warm(); x.arm(); x.t+=100000; const auto event=x.t; x.pulse();
    Packet p; assert(!x.s.start(x.t+offset,p)); // Wait for a closed timebase block.
    for (int i=0;i<5;++i) x.advance();
    p=x.start(); assert(p.value==event && p.role==Role::Start && p.lane==lane);
    x.pulse(); x.advance(); assert(x.start().value==event); // Rear wheel cannot replace timestamp.
    auto f=x.finish(); x.send(f,finishMac); f.flags|=jns::Running;
    auto wrong=f; wrong.attempt++; x.send(wrong,finishMac); assert(x.start().value==event);
    wrong=f; wrong.lane=1; x.send(wrong,finishMac); assert(x.start().value==event);
    x.send(f,finishMac); assert(!x.s.start(x.t+offset,p));
  }
  Fixture x; x.warm(); x.arm(); x.pulse(100); for (int i=0;i<5;++i) x.advance();
  Packet p; assert(!x.s.start(x.t+offset,p));
}
void standing() {
  Fixture x; x.warm(); x.arm(Mode::Standing); x.c.kind=Kind::Go; x.c.value=x.t+4000000; x.send(x.c);
  assert(x.s.state()==StartState::Cued && x.s.cuesActive());
  const uint64_t go=x.c.value; assert(x.s.goLocal()==go+offset);
  auto status=wire(x.s.status(x.t+offset)); assert((status.flags&jns::Cued) && !(status.flags&jns::Running) && status.value==go);
  auto duplicate=x.c; duplicate.value+=100000; x.send(duplicate); assert(x.s.goLocal()==go+offset);
  CuePlayer player; uint64_t times[8]; for (uint8_t i=0;i<8;++i) times[i]=x.s.cueTime(i);
  player.schedule(times);
  for (uint8_t i=0;i<8;++i) {
    assert(player.tick(times[i],times[i]+100000)==(i%2?0:(i==6?2000:1000)));
  }
  assert(!player.failed());
  x.pulse(); while (x.t<go) x.advance();
  assert(x.s.state()==StartState::Running); Packet p; assert(!x.s.start(x.t+offset,p));
  x.c.kind=Kind::Cancel; x.send(x.c); assert(!x.s.cuesActive());
  x.c.kind=Kind::Go; x.send(x.c); assert(!x.s.cuesActive());
  // A late countdown is rejected, not compressed/replayed.
  Fixture late; late.warm(); late.arm(Mode::Standing); late.c.kind=Kind::Go; late.c.value=late.t+3000000; late.send(late.c);
  assert(late.s.state()==StartState::Invalid);
  player.schedule(times); assert(player.tick(times[0]+20001,times[0]+100000)==0 && player.failed());
  player.schedule(times); assert(player.tick(times[0],times[0]-1)==0 && player.failed());
  player.schedule(times); player.stop(); assert(player.tick(times[0],times[0]+100000)==0);
}
void recoveryAndFiltering() {
  { Fixture x; x.warm(); x.arm(); auto p=x.c; p.kind=Kind::Cancel;
    p.system++; x.send(p); assert(x.s.state()==StartState::Armed); p.system--;
    x.send(p,alien); assert(x.s.state()==StartState::Armed);
    p.session++; x.send(p); assert(x.s.state()==StartState::Armed); p.session--;
    x.send(p); x.c.kind=Kind::Arm; x.send(x.c); assert(x.s.state()==StartState::Idle); x.arm(Mode::Flying,2); }
  { Fixture x; x.warm(); x.c.kind=Kind::Cancel; x.c.attempt=5; x.send(x.c);
    x.c.kind=Kind::Arm; x.send(x.c); assert(x.s.state()==StartState::Idle); }
  { Fixture x; x.warm(); x.arm(); x.s.tick(x.t+offset+1600000); assert(x.s.state()==StartState::Invalid); }
  { Fixture x; x.warm(); x.arm(); for (int i=0;i<61;++i) x.advance(); assert(x.s.state()==StartState::Invalid); }
  { Fixture x; x.warm(); x.arm(); x.c.kind=Kind::Reveal; x.c.value=x.t+1000000; x.send(x.c);
    x.pulse(); for (int i=0;i<65;++i) x.advance(); Packet p;
    assert(!x.s.start(x.t+offset,p) && (x.s.status(x.t+offset).flags&jns::Armed)); }
  { Fixture x; x.warm(); x.arm(); x.s.overflow(); x.c.kind=Kind::Cancel; x.send(x.c); assert(!x.s.ready(x.t+offset)); }
  { Fixture x; x.warm(); x.arm(); auto f=x.finish(); x.send(f,finishMac); f.boot++; x.send(f,finishMac);
    assert(x.s.state()==StartState::Invalid); }
  { Fixture x; x.warm(); x.send(x.s.status(x.t+offset),alien); assert(!x.s.ready(x.t+offset)); }
  { Fixture x; x.warm(); x.c.kind=Kind::Status; x.send(x.c,alien); assert(!x.s.ready(x.t+offset)); }
  { Fixture x; x.warm(); x.arm(); auto old=x.c; x.c.session++; x.c.boot++; x.c.attempt=0; x.c.flags=0; x.beat();
    old.kind=Kind::Status; old.value=x.t+100; x.send(old); assert(x.s.status(x.t+offset).session==100 && !x.s.cuesActive()); }
  { Fixture x; x.c.attempt=5; x.c.flags=jns::Running; x.warm(); x.c.kind=Kind::Arm; x.send(x.c);
    assert(x.s.state()==StartState::Idle); }
  { Fixture x; x.warm(); x.s.edge(true,x.t+offset); x.c.attempt=1; x.c.kind=Kind::Arm; x.send(x.c);
    assert(x.s.state()==StartState::Invalid); }
  { Fixture x; x.warm(); x.c.mask=1; x.c.attempt=1; x.c.kind=Kind::Arm; x.send(x.c);
    assert(x.s.state()==StartState::Invalid); }
}
// Actual controller and all three start engines; finish statuses are explicit
// protocol fixtures, not a claim that FN-2–FN-4 have been implemented.
void controllerIntegration(Mode mode) {
  Controller c(42,99); c.lanes=4; c.mode=mode;
  Fixture a(2),b(3),d(4); Fixture* starts[]={&a,&b,&d};
  uint64_t t=10000000;
  auto packet=[&](Kind kind) { Packet p; p.kind=kind; p.system=42; p.session=99; p.boot=7;
    p.attempt=c.attempt; p.mode=mode; p.mask=15; p.value=t;
    if (c.active()) p.flags|=jns::Armed;
    return p; };
  auto exchange=[&]() {
    for (auto s:starts) { s->t=t; s->c=packet(Kind::Status); s->beat();
      auto p=wire(s->s.status(t+offset)); uint8_t mac[]={2,0,0,0,2,p.lane}; c.observe(mac,p,t); }
    for (uint8_t lane=1;lane<=4;++lane) {
      auto p=packet(Kind::Status); p.role=Role::Finish; p.lane=lane; p.boot=30+lane; p.flags|=jns::Ready;
      if (c.goAt) { p.flags|=jns::Cued; p.value=c.goAt; }
      uint8_t mac[]={2,0,0,0,3,lane}; c.observe(mac,wire(p),t);
    }
  };
  for (int i=0;i<20;++i,t+=500000) exchange();
  assert(c.arm(t,true)); for (auto s:starts) s->send(packet(Kind::Arm));
  exchange(); c.tick(t,true); assert(c.state==State::Armed);
  if (mode==Mode::Standing) {
    assert(c.countdown(t)); auto p=packet(Kind::Go); p.value=c.goAt;
    for (auto s:starts) s->send(p);
    exchange(); assert(c.cued(t));
    for (int i=0;i<8;++i) { t+=500000; exchange(); c.tick(t,true); }
    assert(c.state==State::Running && c.started==15);
    for (auto s:starts) assert(s->s.state()==StartState::Running);
  } else {
    t+=100000; assert(c.localStart(t));
    for (auto s:starts) { s->t=t; s->pulse(); }
    for (int i=0;i<5;++i) { t+=500000; exchange(); }
    for (auto s:starts) { auto p=s->start(); uint8_t mac[]={2,0,0,0,2,p.lane}; assert(c.observe(mac,p,t)); }
    assert(c.started==15 && c.state==State::Running);
  }
}
int main() { flying(); standing(); recoveryAndFiltering(); controllerIntegration(Mode::Flying);
  controllerIntegration(Mode::Standing); puts("All external start-sensor tests passed"); }
