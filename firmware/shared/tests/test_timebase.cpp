#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "lanetime/timebase.hpp"
using namespace jns;

// Deterministic simulation. ST-1 time is the true time t (µs). A unit's clock
// runs at (1+drift) with an arbitrary offset. Each heartbeat is stamped by
// ST-1 at t and received after base + extra delay.
struct Rng {
  uint64_t s;
  double uni() { s=s*6364136223846793005ULL+1442695040888963407ULL; return double(s>>11)/9007199254740992.0; }
};
struct Unit {
  double drift, offset;
  uint64_t local(double t) const { return uint64_t(llround(t*(1.0+drift)+offset)); }
};
struct Link {
  double base=800, pDelayed=0.5, maxExtra=5000;  // pessimistic channel
  double delay(Rng& r) const { return base+(r.uni()<pDelayed ? r.uni()*maxExtra : r.uni()*30); }
};
constexpr double kStep=500000, kT0=1e9;  // ST-1 has been up ~17 minutes
constexpr uint64_t kSession=77;

double feed(Timebase& tb,const Unit& u,Rng& r,const Link& link,double from,double to,uint64_t session=kSession) {
  double t=from;
  for (;t<to;t+=kStep) tb.sample(session,u.local(t+link.delay(r)),uint64_t(t));
  return t;
}
double converted(const Timebase& tb,const Unit& u,double te) {
  uint64_t r=0; assert(tb.interpolable(u.local(te))); assert(tb.toRemote(u.local(te),r)); return double(r);
}

void readiness() {
  Timebase tb; Unit u{25e-6,5e9}; Rng r{1}; Link perfect; perfect.pDelayed=0; perfect.base=800;
  double t=feed(tb,u,r,perfect,kT0,kT0+(kTbMinBlocks*kTbBlockSamples-1)*kStep);
  assert(!tb.ready(u.local(t)));                       // One sample short of four blocks.
  t=feed(tb,u,r,perfect,t,t+kStep); assert(tb.ready(u.local(t)));
  assert(!tb.ready(u.local(t+kTbMaxAgeUs+kStep)));    // Stale.
  assert(fabs(tb.drift()-25e-6)<2e-6);
}
void perfectLink() {
  Timebase tb; Unit u{-40e-6,3.3e9}; Rng r{2}; Link link; link.pDelayed=0; link.maxExtra=0;
  const double te=kT0+9.3e6;
  double t=feed(tb,u,r,link,kT0,te);
  assert(!tb.interpolable(u.local(te)));               // No block minimum after the event yet.
  feed(tb,u,r,link,t,t+kTbBlockSamples*kStep);
  // Constant one-way delay biases every unit equally; it cancels in elapsed times.
  assert(fabs(converted(tb,u,te)-(te-link.base))<40);
  uint64_t back=0, rem=0; assert(tb.toRemote(u.local(te),rem) && tb.toLocal(rem,back));
  assert(llabs(int64_t(back)-int64_t(u.local(te)))<=1);
}
// The bench test in software: one true event, two units, compare converted times.
void twoUnitSync() {
  Unit a{25e-6,5e9}, b{-15e-6,2.1e10}; Rng ra{3}, rb{4}; Link link;
  Timebase ta, tb; double worst=0;
  for (int k=0;k<60;++k) {
    const double te=kT0+2e7+k*1.37e6;                  // Events every 1.37 s over ~80 s.
    const double from=k?te-1.37e6+3e6:kT0, end=te+3e6;
    const double lastA=feed(ta,a,ra,link,from,end)-kStep, lastB=feed(tb,b,rb,link,from,end)-kStep;
    if (k<2) continue;
    assert(ta.ready(a.local(lastA)) && tb.ready(b.local(lastB)));
    worst=fmax(worst,fabs(converted(ta,a,te)-converted(tb,b,te)));
  }
  printf("  two-unit sync, pessimistic link: worst %.1f us\n",worst);
  assert(worst<50);
}
void delayedBlocksRejected() {
  Timebase tb; Unit u{10e-6,7e9}; Rng r{5}; Link good; good.pDelayed=0;
  double t=kT0;
  for (int block=0;block<12;++block)                  // Every third block entirely late by 3 ms.
    for (int i=0;i<kTbBlockSamples;++i,t+=kStep)
      tb.sample(kSession,u.local(t+good.delay(r)+(block%3==1?3000:0)),uint64_t(t));
  assert(tb.ready(u.local(t)) && tb.kept()<kTbBlocks && tb.spread()<kTbSpreadUs);
  const double te=t-3*kStep; assert(fabs(converted(tb,u,te)-(te-good.base))<40);
}
void resets() {
  Timebase tb; Unit u{0,1e9}; Rng r{6}; Link link; link.pDelayed=0;
  double t=feed(tb,u,r,link,kT0,kT0+2e7); assert(tb.ready(u.local(t)));
  tb.sample(kSession+1,u.local(t),uint64_t(t));       // ST-1 rebooted.
  assert(!tb.ready(u.local(t)) && tb.session()==kSession+1);
  t=feed(tb,u,r,link,t+kStep,t+2e7,kSession+1); assert(tb.ready(u.local(t)));
  t+=kTbGapUs+kStep; tb.sample(kSession+1,u.local(t),uint64_t(t));  // Long silence.
  assert(!tb.ready(u.local(t)));
  Timebase empty; uint64_t x=0; assert(!empty.toRemote(1,x) && !empty.toLocal(1,x) && !empty.interpolable(0));
}

int main() {
  readiness(); perfectLink(); twoUnitSync(); delayedBlocksRejected(); resets();
  puts("All timebase tests passed");
}
