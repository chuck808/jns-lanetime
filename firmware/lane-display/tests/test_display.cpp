#include <cassert>
#include <cstdio>
#include <cstring>
#include "lane_display.hpp"
#include "display_text.hpp"
using namespace jns;
const uint8_t control[]={2,0,0,0,1,1}, finish[]={2,0,0,0,3,1}, alien[]={2,0,0,0,9,9};
constexpr uint64_t base=10000000, offset=1000000;
struct Fixture {
  LaneDisplay d;
  Packet c;
  uint64_t remote=base;
  explicit Fixture(uint8_t lane=1):d(42,lane) {
    c.system=42; c.session=99; c.boot=7; c.kind=Kind::Status;
  }
  uint64_t local() const { return remote+offset; }
  void beat() { c.kind=Kind::Status; c.value=remote; d.receive(control,c,local()); d.tick(local()); }
  void warm() { for (int i=0;i<20;++i) { beat(); remote+=500000; } assert(d.synced(local())); }
  void arm(uint32_t n=1,uint8_t mask=1) {
    c.attempt=n; c.mask=mask; c.flags=jns::Armed;
    c.kind=Kind::Arm; c.value=0; d.receive(control,c,local());
  }
  Packet peer(uint8_t lane=1) const {
    Packet p=c; p.role=Role::Finish; p.lane=lane; p.boot=9;
    p.kind=Kind::Status; p.value=0; p.flags=jns::Armed; return p;
  }
  void result(uint64_t value=1750000,uint8_t lane=1) {
    auto p=peer(lane); d.receive(finish,p,local()); p.kind=Kind::Result;
    p.flags=jns::Complete; p.value=value; d.receive(finish,p,local());
  }
  void reveal(uint8_t mask=1) {
    c.kind=Kind::Reveal; c.value=remote+1000000; c.finishedMask=mask;
    d.receive(control,c,local()); c.finishedMask=0;
  }
  void advance() { remote+=500000; beat(); }
};
void happyPath() {
  Fixture f; f.warm(); assert(f.d.state()==DisplayState::Idle); f.arm(); f.result();
  assert(f.d.state()==DisplayState::Pending); f.reveal();
  f.advance(); assert(f.d.state()==DisplayState::Pending);
  f.d.tick(f.local()+499999); assert(f.d.state()==DisplayState::Pending);
  f.advance(); assert(f.d.state()==DisplayState::Time && f.d.elapsed()==1750000);
  f.result(999); assert(f.d.elapsed()==1750000);
  f.arm(); assert(f.d.state()==DisplayState::Time); // Duplicate ARM does not clear output.
  f.arm(2); assert(f.d.state()==DisplayState::Pending && !f.d.elapsed());
  auto old=f.peer(); old.attempt=1; old.kind=Kind::Result; old.flags=jns::Complete; old.value=2000000;
  f.d.receive(finish,old,f.local()); f.reveal(); f.advance(); f.advance();
  assert(f.d.state()==DisplayState::Unavailable);
}
void masksAndMissingData() {
  { Fixture f; f.warm(); f.arm(); f.result(); f.reveal(0); f.advance(); f.advance();
    assert(f.d.state()==DisplayState::Dnf); }
  { Fixture f; f.warm(); f.arm(); f.reveal(); f.advance(); f.result(); f.advance();
    assert(f.d.state()==DisplayState::Time); } // Result may follow Reveal but precede its deadline.
  { Fixture f; f.warm(); f.arm(); f.reveal(); f.advance(); f.advance(); f.result();
    assert(f.d.state()==DisplayState::Unavailable); } // No late visual change after reveal.
  { Fixture f(4); f.warm(); f.arm(1,7); f.result(123,4); f.reveal(7); f.advance(); f.advance();
    assert(f.d.state()==DisplayState::Disabled); }
  { Fixture f(4); f.warm(); f.arm(1,15); f.result(2345000,4); f.reveal(8); f.advance(); f.advance();
    assert(f.d.state()==DisplayState::Time && f.d.elapsed()==2345000); }
}
void filtering() {
  Fixture f; f.warm(); f.arm();
  auto p=f.peer(); p.kind=Kind::Result; p.flags=jns::Complete; p.value=123;
  f.d.receive(finish,p,f.local()); assert(!f.d.elapsed()); // Requires announced finish identity.
  p=f.peer(); f.d.receive(finish,p,f.local()); p.kind=Kind::Result; p.flags=jns::Complete; p.value=123;
  auto bad=p; bad.lane=2; f.d.receive(finish,bad,f.local());
  bad=p; bad.session=100; f.d.receive(finish,bad,f.local());
  bad=p; bad.system=43; f.d.receive(finish,bad,f.local());
  bad=p; bad.boot=10; f.d.receive(finish,bad,f.local());
  bad=p; bad.attempt=2; f.d.receive(finish,bad,f.local());
  bad=p; bad.mode=Mode::Standing; f.d.receive(finish,bad,f.local());
  bad=p; bad.value=30000001; f.d.receive(finish,bad,f.local());
  bad=p; bad.value=0; f.d.receive(finish,bad,f.local());
  bad=p; bad.flags|=jns::Fault; f.d.receive(finish,bad,f.local());
  f.d.receive(alien,p,f.local()); assert(!f.d.elapsed());
  f.d.receive(finish,p,f.local()); assert(f.d.elapsed()==123);
}
void cancellations() {
  { Fixture f; f.warm(); f.arm(); f.result(); f.reveal();
    f.c.kind=Kind::Cancel; f.d.receive(control,f.c,f.local());
    f.arm(); f.result(); f.reveal(); f.advance(); f.advance();
    assert(f.d.state()==DisplayState::Unavailable && !f.d.elapsed());
    f.arm(2); assert(f.d.state()==DisplayState::Pending); }
  { Fixture f; f.warm(); f.c.attempt=2; f.c.kind=Kind::Cancel;
    f.d.receive(control,f.c,f.local()); f.arm(2); assert(f.d.state()==DisplayState::Unavailable); }
  { Fixture f; f.warm(); f.arm(); f.result(); f.reveal(); f.c.flags=jns::Fault; f.advance();
    assert(f.d.state()==DisplayState::Unavailable && !f.d.elapsed()); }
}
void identities() {
  { Fixture f; f.warm(); f.arm(); f.result(); auto p=f.peer(); p.boot++;
    f.d.receive(finish,p,f.local()); assert(f.d.state()==DisplayState::Unavailable); }
  { Fixture f; f.warm(); f.arm(); f.result(); auto p=f.peer();
    f.d.receive(alien,p,f.local()); assert(f.d.state()==DisplayState::Unavailable); }
  { Fixture f; f.warm(); f.c.kind=Kind::Status; f.c.value=f.remote;
    f.d.receive(alien,f.c,f.local()); assert(f.d.state()==DisplayState::Fault);
    f.arm(); assert(f.d.state()==DisplayState::Fault); }
  { Fixture f; f.warm(); f.arm(); f.result(); auto old=f.c;
    f.c.session=100; f.c.boot=8; f.c.attempt=0; f.remote=1; f.beat();
    assert(f.d.state()==DisplayState::Idle && !f.d.elapsed() && !f.d.synced(f.local()));
    old.kind=Kind::Status; old.value=99999999; f.d.receive(control,old,100000000);
    assert(f.d.state()==DisplayState::Idle); f.warm(); f.arm(); f.result();
    assert(f.d.state()==DisplayState::Pending); }
}
void expiryAndRevealValidation() {
  { Fixture f; f.warm(); f.arm(); f.result(); f.reveal(); f.advance(); f.advance();
    f.d.tick(f.local()+3000001); assert(f.d.state()==DisplayState::Offline && !f.d.elapsed());
    f.remote+=4000000; f.beat(); f.arm(); f.result(); f.reveal();
    assert(f.d.state()!=DisplayState::Time); }
  { Fixture f; f.warm(); f.arm(); f.reveal(); f.d.tick(f.local()+1600000);
    assert(f.d.state()==DisplayState::Unavailable); } // Sync freshness shorter than link timeout.
  { Fixture f; f.warm(); f.arm(); f.reveal(); f.c.kind=Kind::Reveal;
    f.c.value=f.remote+1500000; f.c.finishedMask=1; f.d.receive(control,f.c,f.local());
    assert(f.d.state()==DisplayState::Unavailable); }
  { Fixture f; f.warm(); f.arm(); f.c.kind=Kind::Reveal; f.c.value=f.remote;
    f.d.receive(control,f.c,f.local()); assert(f.d.state()==DisplayState::Unavailable); }
  { Fixture f; f.warm(); f.arm(); f.c.kind=Kind::Reveal; f.c.value=f.remote+3000000;
    f.d.receive(control,f.c,f.local()); assert(f.d.state()==DisplayState::Unavailable); }
  { Fixture f; f.beat(); f.arm(); f.reveal(); assert(f.d.state()==DisplayState::Unavailable); }
  { Fixture f; f.warm(); f.arm(); f.result(); f.d.overflow(); f.reveal();
    assert(f.d.state()==DisplayState::Fault); }
  { Fixture f; f.warm(); f.arm(); for (int i=0;i<65;++i) f.advance();
    assert(f.d.state()==DisplayState::Unavailable); }
  { Fixture f; f.warm(); f.arm(); f.result(); f.reveal(); f.reveal();
    f.advance(); f.advance(); assert(f.d.state()==DisplayState::Time); }
}
void wireToFourDisplays() {
  LaneDisplay displays[]={LaneDisplay(42,1),LaneDisplay(42,2),LaneDisplay(42,3),LaneDisplay(42,4)};
  const uint64_t offsets[]={1000000,3000000,7000000,11000000};
  auto deliver=[&](int i,const uint8_t* mac,const Packet& p,uint64_t remote) {
    uint8_t bytes[kPacketSize]; encode(p,bytes); Packet decoded;
    assert(decode(bytes,sizeof(bytes),decoded));
    displays[i].receive(mac,decoded,remote+offsets[i]);
    displays[i].tick(remote+offsets[i]);
  };
  Packet c; c.system=42; c.session=99; c.boot=7; c.mask=15;
  for (uint64_t t=10000000;t<20000000;t+=500000) {
    c.value=t; for (int i=0;i<4;++i) deliver(i,control,c,t);
  }
  c.kind=Kind::Arm; c.attempt=1; c.value=0;
  for (int i=0;i<4;++i) deliver(i,control,c,20000000);
  for (int i=0;i<4;++i) {
    auto p=c; p.kind=Kind::Status; p.role=Role::Finish; p.lane=i+1;
    p.boot=10+i; p.flags=jns::Armed; deliver(i,finish,p,20100000);
    p.kind=Kind::Result; p.flags=jns::Complete; p.value=1750000+i*100000;
    if (i!=2) deliver(i,finish,p,20200000); // Lane 3 loses its Result despite finishing.
  }
  c.kind=Kind::Reveal; c.value=21500000; c.finishedMask=7;
  for (int i=0;i<4;++i) deliver(i,control,c,20300000+i*100000); // Different copies survive.
  c.kind=Kind::Status; c.finishedMask=0; c.flags=jns::Complete; c.value=21000000;
  for (int i=0;i<4;++i) {
    deliver(i,control,c,21000000);
    displays[i].tick(21499999+offsets[i]); assert(displays[i].state()==DisplayState::Pending);
    displays[i].tick(21500000+offsets[i]);
  }
  assert(displays[0].state()==DisplayState::Time && displays[0].elapsed()==1750000);
  assert(displays[1].state()==DisplayState::Time && displays[1].elapsed()==1850000);
  assert(displays[2].state()==DisplayState::Unavailable);
  assert(displays[3].state()==DisplayState::Dnf);
}
void formatting() {
  char out[8]; displayText(DisplayState::Time,1750000,true,out); assert(!strcmp(out,"1.750"));
  displayText(DisplayState::Time,9999500,true,out); assert(!strcmp(out,"10.000"));
  displayText(DisplayState::Time,30000000,true,out); assert(!strcmp(out,"30.000"));
  displayText(DisplayState::Time,30000001,true,out); assert(!strcmp(out,"ERR"));
  displayText(DisplayState::Dnf,0,true,out); assert(!strcmp(out,"DNF"));
  displayText(DisplayState::Pending,0,false,out); assert(!strcmp(out,"SYNC"));
  displayText(DisplayState::Offline,0,false,out); assert(!strcmp(out,"----"));
}
int main() {
  happyPath(); masksAndMissingData(); filtering(); cancellations(); identities();
  expiryAndRevealValidation(); wireToFourDisplays(); formatting(); puts("All lane-display tests passed");
}
