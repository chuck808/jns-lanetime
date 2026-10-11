#include <cassert>
#include <cstdio>
#include "finish_sensor.hpp"
#include "start_sensor.hpp"
#include "controller.hpp"
#include "lane_display.hpp"
using namespace jns;
const uint8_t controlMac[]={2,0,0,0,1,1};
Packet wire(const Packet& p) { uint8_t bytes[kPacketSize]; encode(p,bytes); Packet out; assert(decode(bytes,sizeof(bytes),out)); return out; }
struct Rig {
  Controller c{42,99};
  StartSensor starts[3]={{42,12,2},{42,13,3},{42,14,4}};
  FinishSensor finishes[4]={{42,21,1},{42,22,2},{42,23,3},{42,24,4}};
  LaneDisplay displays[4]={{42,1},{42,2},{42,3},{42,4}};
  uint64_t t=10000000;
  const uint64_t startOffset[3]={1000000,2200000,3700000};
  const uint64_t finishOffset[4]={500000,1500000,2800000,4100000};
  const uint64_t displayOffset[4]={250000,750000,1900000,3100000};
  explicit Rig(Mode mode=Mode::Flying) {
    c.mode=mode; c.lanes=4;
    for (int i=0;i<3;++i) starts[i].edge(false,t+startOffset[i]);
    for (int i=0;i<4;++i) finishes[i].edge(false,t+finishOffset[i]);
  }
  Packet controllerPacket(Kind kind) {
    Packet p; p.kind=kind; p.system=42; p.session=99; p.boot=7;
    p.attempt=c.attempt; p.mode=c.mode; p.mask=c.mask();
    if (c.state==State::Armed || c.state==State::Countdown || c.state==State::Running) p.flags|=jns::Armed;
    if (c.state==State::Running) p.flags|=jns::Running;
    if (c.state==State::Revealing || c.state==State::Complete) p.flags|=jns::Complete;
    if (c.state==State::Fault) p.flags|=jns::Fault;
    if (kind==Kind::Status) p.value=t;
    return p;
  }
  void broadcastController(Packet p) {
    p=wire(p);
    for (int i=0;i<3;++i) starts[i].receive(controlMac,p,t+startOffset[i]);
    for (int i=0;i<4;++i) {
      finishes[i].receive(controlMac,p,t+finishOffset[i]);
      displays[i].receive(controlMac,p,t+displayOffset[i]);
    }
  }
  void statuses() {
    broadcastController(controllerPacket(Kind::Status));
    for (int i=0;i<3;++i) {
      starts[i].tick(t+startOffset[i]);
      auto p=wire(starts[i].status(t+startOffset[i]));
      uint8_t mac[]={2,0,0,0,2,uint8_t(i+2)};
      c.observe(mac,p,t);
      for (int j=0;j<4;++j) finishes[j].receive(mac,p,t+finishOffset[j]);
    }
    for (int i=0;i<4;++i) {
      finishes[i].tick(t+finishOffset[i]);
      auto p=wire(finishes[i].status(t+finishOffset[i]));
      uint8_t mac[]={2,0,0,0,3,uint8_t(i+1)};
      c.observe(mac,p,t);
      for (int j=0;j<3;++j) starts[j].receive(mac,p,t+startOffset[j]);
      for (int j=0;j<4;++j) displays[j].receive(mac,p,t+displayOffset[j]);
    }
  }
  void warmArm() {
    for (int i=0;i<20;++i,t+=500000) statuses();
    assert(c.arm(t,true)); broadcastController(controllerPacket(Kind::Arm));
    statuses(); c.tick(t,true); assert(c.state==State::Armed);
    for (auto& f:finishes) assert(f.state()==FinishState::Armed);
  }
  void startPulse(int index) {
    starts[index].edge(true,t+startOffset[index]); starts[index].edge(false,t+startOffset[index]+300);
  }
  void finishPulse(int index) {
    finishes[index].edge(true,t+finishOffset[index]); finishes[index].edge(false,t+finishOffset[index]+300);
  }
  void advance() { t+=100000; if (t%500000==0) statuses(); }
};
void fourLanes(Mode mode,bool partial=false,bool missingDisplay=false,bool cancelReveal=false) {
  Rig r(mode); r.warmArm(); const uint64_t armed=r.t;
  uint64_t begin=armed+100000;
  if (mode==Mode::Standing) {
    assert(r.c.countdown(r.t)); auto go=r.controllerPacket(Kind::Go); go.value=r.c.goAt;
    r.broadcastController(go); r.statuses(); assert(r.c.cued(r.t)); begin=r.c.goAt;
  }
  const uint64_t elapsed[4]={1500000,1700000,1900000,2100000};
  bool started[4]={},finished[4]={}; unsigned sends[3]={},reveals=0; bool cancelled=false;
  for (int step=0;step<140;++step) {
    r.advance();
    for (int i=0;i<4;++i) if (!started[i] && r.t>=begin+(mode==Mode::Flying?i*100000:0)) {
      started[i]=true;
      if (mode==Mode::Flying) {
        if (!i) assert(r.c.localStart(r.t)); else r.startPulse(i-1);
      }
    }
    if (mode==Mode::Flying && r.c.startPending(r.t)) {
      auto p=r.controllerPacket(Kind::Start); p.value=r.c.startAt; r.broadcastController(p);
    }
    for (int i=0;i<4;++i) if (!finished[i] && r.t>=begin+elapsed[i]+(mode==Mode::Flying?i*100000:0) && !(partial && i==3)) {
      r.finishPulse(i); finished[i]=true;
    }
    for (int i=0;i<3;++i) {
      r.starts[i].tick(r.t+r.startOffset[i]); Packet p;
      if (r.starts[i].start(r.t+r.startOffset[i],p)) {
        // Drop the first copy; hold lane 3 START until after its finish edge.
        if (++sends[i]==1 || (i==1 && r.t<begin+3000000)) continue;
        p=wire(p); uint8_t mac[]={2,0,0,0,2,uint8_t(i+2)};
        r.c.observe(mac,p,r.t);
        for (int j=0;j<4;++j) r.finishes[j].receive(mac,p,r.t+r.finishOffset[j]);
      }
    }
    for (int i=0;i<4;++i) {
      r.finishes[i].tick(r.t+r.finishOffset[i]); Packet p;
      if (r.finishes[i].result(r.t+r.finishOffset[i],p)) {
        p=wire(p); uint8_t mac[]={2,0,0,0,3,uint8_t(i+1)};
        r.c.observe(mac,p,r.t);
        if (!(missingDisplay && i==2)) for (int j=0;j<4;++j)
          r.displays[j].receive(mac,p,r.t+r.displayOffset[j]);
      }
    }
    if (partial && r.c.state==State::Running && r.c.finished==7) assert(r.c.reveal(r.t));
    r.c.tick(r.t,true);
    assert(r.c.state!=State::Fault);
    if (r.c.state==State::Revealing) {
      auto p=r.controllerPacket(Kind::Reveal); p.value=r.c.revealAt; p.finishedMask=r.c.finished;
      if (++reveals>2) r.broadcastController(p); // Lose two Reveal copies.
      if (cancelReveal && reveals==4) {
        r.c.cancel(); r.broadcastController(r.controllerPacket(Kind::Cancel)); cancelled=true;
      }
    }
    for (int i=0;i<4;++i) r.displays[i].tick(r.t+r.displayOffset[i]);
    if (cancelled) break;
  }
  if (cancelReveal) {
    assert(cancelled);
    for (int i=0;i<4;++i) { Packet p; assert(!r.finishes[i].result(r.t+r.finishOffset[i],p));
      assert(r.displays[i].state()!=DisplayState::Time); }
    return;
  }
  assert(r.c.state==State::Complete && r.c.finished==(partial?7:15));
  for (int i=0;i<4;++i) {
    if (partial && i==3) assert(r.displays[i].state()==DisplayState::Dnf);
    else if (missingDisplay && i==2) assert(r.displays[i].state()==DisplayState::Unavailable);
    else { assert(r.c.results[i]==elapsed[i]); assert(r.displays[i].state()==DisplayState::Time);
      assert(r.displays[i].elapsed()==elapsed[i]); }
  }
}
void externalIdentity() {
  { Rig r; r.warmArm(); r.t+=100000; auto p=r.controllerPacket(Kind::Start); p.value=r.t;
    r.broadcastController(p); assert(r.finishes[1].state()==FinishState::Armed); // Controller START is lane 1 only.
    p.role=Role::Start; p.lane=2; p.boot=12; uint8_t mac[]={2,0,0,0,2,2};
    auto wrong=p; wrong.lane=3; r.finishes[1].receive(mac,wire(wrong),r.t+r.finishOffset[1]);
    wrong=p; wrong.boot++; r.finishes[1].receive(mac,wire(wrong),r.t+r.finishOffset[1]);
    wrong=p; wrong.session++; r.finishes[1].receive(mac,wire(wrong),r.t+r.finishOffset[1]);
    wrong=p; wrong.attempt++; r.finishes[1].receive(mac,wire(wrong),r.t+r.finishOffset[1]);
    assert(r.finishes[1].state()==FinishState::Armed);
    r.finishes[1].receive(mac,wire(p),r.t+r.finishOffset[1]); assert(r.finishes[1].state()==FinishState::Running);
  }
  { Rig r; r.warmArm(); auto p=r.starts[0].status(r.t+r.startOffset[0]); p.boot++;
    uint8_t mac[]={2,0,0,0,2,2}; r.finishes[1].receive(mac,wire(p),r.t+r.finishOffset[1]);
    assert(r.finishes[1].state()==FinishState::Invalid);
  }
  { Rig r; r.warmArm(); auto p=r.starts[0].status(r.t+r.startOffset[0]); uint8_t mac[]={2,0,0,9,2,2};
    r.finishes[1].receive(mac,wire(p),r.t+r.finishOffset[1]);
    assert(r.finishes[1].state()==FinishState::Invalid && !r.finishes[1].ready(r.t+r.finishOffset[1]));
  }
  { Rig r; r.warmArm(); // Controller stays fresh; the matching start post disappears.
    for (int i=0;i<7;++i) { r.t+=500000; r.broadcastController(r.controllerPacket(Kind::Status));
      r.finishes[1].tick(r.t+r.finishOffset[1]); }
    assert(r.finishes[1].state()==FinishState::Invalid);
  }
  { Rig r; // FN-2 cannot advertise Ready with no ST-2 discovery.
    for (int i=0;i<20;++i,r.t+=500000) r.broadcastController(r.controllerPacket(Kind::Status));
    assert(!r.finishes[1].ready(r.t+r.finishOffset[1]));
  }
  { Rig r; r.warmArm(); r.c.cancel(); r.broadcastController(r.controllerPacket(Kind::Cancel));
    r.t+=500000; r.statuses(); assert(r.c.arm(r.t,true));
    r.broadcastController(r.controllerPacket(Kind::Arm)); r.statuses(); r.c.tick(r.t,true);
    assert(r.c.attempt==2 && r.c.state==State::Armed);
    for (auto& f:r.finishes) assert(f.state()==FinishState::Armed);
  }
  { Rig r; r.warmArm(); auto p=r.starts[0].status(r.t+r.startOffset[0]);
    uint8_t mac[]={2,0,0,0,2,2}; p.flags=jns::Ready;
    r.finishes[1].receive(mac,wire(p),r.t+r.finishOffset[1]);
    p.kind=Kind::Start; p.value=r.t; r.finishes[1].receive(mac,wire(p),r.t+r.finishOffset[1]);
    assert(r.finishes[1].state()==FinishState::Armed); // No Armed announcement, no START acceptance.
  }

}
int main() {
  fourLanes(Mode::Flying); fourLanes(Mode::Standing);
  fourLanes(Mode::Flying,true); fourLanes(Mode::Standing,true);
  fourLanes(Mode::Flying,false,true); fourLanes(Mode::Flying,false,false,true);
  externalIdentity(); puts("All four-lane finish/integration tests passed");
}
