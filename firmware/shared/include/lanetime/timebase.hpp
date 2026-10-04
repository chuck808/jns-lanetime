#pragma once
// Tracks ST-1's clock from its Status heartbeats (specification §5.1).
// Pure logic: no Arduino/ESP-IDF dependency, so it is tested natively.
// Not thread-safe: the caller serialises sample() (radio callback) against
// conversions (main loop), e.g. by queueing samples to the loop as ST-1 does.
#include <math.h>
#include <stdint.h>

namespace jns {

// Provisional values; the bench sync test (§9) replaces the thresholds.
constexpr uint8_t kTbBlockSamples=4;      // heartbeats per block (~2 s)
constexpr uint8_t kTbBlocks=8;            // block minima kept (~16 s)
constexpr uint8_t kTbMinBlocks=4;         // kept minima needed for Ready
constexpr uint64_t kTbMaxAgeUs=1500000;   // newest sample age for Ready
constexpr uint64_t kTbGapUs=3000000;      // longer silence discards history
constexpr double kTbRejectUs=200;         // one-sided: above-line residual dropped
constexpr double kTbSpreadUs=100;         // RMS residual allowed for Ready

class Timebase {
 public:
  // Record one ST-1 heartbeat: local = receive-callback time, remote = ST-1
  // clock carried in the packet. A new ST-1 session discards all history.
  void sample(uint64_t session,uint64_t local,uint64_t remote) {
    if (session!=session_ || (count_ && local<=last_) || (count_ && local-last_>kTbGapUs)) clear(session);
    const int64_t o=int64_t(local)-int64_t(remote);
    if (!inBlock_ || o<blockMin_.o) blockMin_={local,o};
    last_=local; ++count_;
    if (++inBlock_==kTbBlockSamples) {
      points_[next_]=blockMin_; next_=(next_+1)%kTbBlocks;
      if (stored_<kTbBlocks) ++stored_;
      inBlock_=0; closed_=local; fit();
    }
  }
  bool ready(uint64_t now) const {
    // A sample stamped after `now` (callback raced the loop's clock read) counts as fresh.
    return fitted_ && count_ && (now<=last_ || now-last_<=kTbMaxAgeUs) && spread_<=kTbSpreadUs;
  }
  // An event is converted by interpolation once a block that closed after it is in the fit,
  // so the fit has data from both sides of the event.
  bool interpolable(uint64_t local) const { return fitted_ && closed_>local; }
  // Local → ST-1 time. Valid only when interpolable() (events) or ready() (scheduling).
  bool toRemote(uint64_t local,uint64_t& remote) const {
    if (!fitted_) return false;
    remote=uint64_t(int64_t(local)-offsetAt(local)); return true;
  }
  // ST-1 → local time, for scheduling cues and reveals. Solves L = R + o(L).
  bool toLocal(uint64_t remote,uint64_t& local) const {
    if (!fitted_) return false;
    const double d=double(int64_t(remote)-int64_t(ref_)+oRef_)+a_;
    local=uint64_t(int64_t(ref_)+int64_t(::llround(d/(1.0-b_)))); return true;
  }
  uint64_t session() const { return session_; }
  double drift() const { return b_; }        // relative frequency difference
  double spread() const { return spread_; }  // RMS residual of kept minima, µs
  uint8_t kept() const { return kept_; }

 private:
  struct Point { uint64_t l; int64_t o; };
  void clear(uint64_t session) {
    session_=session; count_=inBlock_=stored_=next_=kept_=0; fitted_=false;
    last_=closed_=ref_=0; oRef_=0; a_=b_=0; spread_=1e9;
  }
  int64_t offsetAt(uint64_t local) const {
    return oRef_+int64_t(::llround(a_+b_*double(int64_t(local)-int64_t(ref_))));
  }
  // Least squares o = a + b·L through the stored minima, centred on the newest
  // point for precision; drop points far above the line once, then refit.
  void fit() {
    if (stored_<kTbMinBlocks) return;
    const Point& newest=points_[(next_+kTbBlocks-1)%kTbBlocks];
    ref_=newest.l; oRef_=newest.o;
    bool keep[kTbBlocks]; for (uint8_t i=0;i<kTbBlocks;++i) keep[i]=i<stored_;
    double a=0,b=0; uint8_t n=0;
    for (int pass=0;pass<2;++pass) {
      if (!solve(keep,a,b,n)) { fitted_=false; return; }
      if (pass) break;
      for (uint8_t i=0;i<stored_;++i) if (residual(points_[i],a,b)>kTbRejectUs) keep[i]=false;
    }
    double sum=0; for (uint8_t i=0;i<stored_;++i) if (keep[i]) { const double r=residual(points_[i],a,b); sum+=r*r; }
    a_=a; b_=b; kept_=n; spread_=n?::sqrt(sum/n):1e9; fitted_=n>=kTbMinBlocks;
  }
  double x(const Point& p) const { return double(int64_t(p.l)-int64_t(ref_)); }
  double y(const Point& p) const { return double(p.o-oRef_); }
  double residual(const Point& p,double a,double b) const { return y(p)-(a+b*x(p)); }
  bool solve(const bool* keep,double& a,double& b,uint8_t& n) const {
    double sx=0,sy=0,sxx=0,sxy=0; n=0;
    for (uint8_t i=0;i<stored_;++i) if (keep[i]) {
      const double px=x(points_[i]),py=y(points_[i]);
      sx+=px; sy+=py; sxx+=px*px; sxy+=px*py; ++n;
    }
    if (n<2) return false;
    const double den=n*sxx-sx*sx; if (den<=0) return false;
    b=(n*sxy-sx*sy)/den; a=(sy-b*sx)/n; return true;
  }

  Point points_[kTbBlocks]={}, blockMin_={0,0};
  uint64_t session_=0, last_=0, closed_=0, ref_=0;
  int64_t oRef_=0;
  double a_=0, b_=0, spread_=1e9;
  uint32_t count_=0;
  uint8_t inBlock_=0, stored_=0, next_=0, kept_=0;
  bool fitted_=false;
};

}  // namespace jns
