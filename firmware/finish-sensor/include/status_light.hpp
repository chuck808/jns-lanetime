#pragma once
#include <stdint.h>
#include "finish_sensor.hpp"
namespace jns {
// What the single status NeoPixel shows. Alignment comes first while idle, because
// that is when someone is standing at the post adjusting it.
enum class Light : uint8_t { Off, Red, Amber, Green, Blue, White };
struct LightView { Light colour=Light::Off; bool lit=false; };
// A beam that keeps dropping out is badly aimed or near its range limit, even if it
// reads clear most of the time. Two or more breaks within a second, while idle, is
// "unsteady". Provisional: tune on the real optics.
class BeamWatch {
 public:
  static constexpr uint64_t windowUs=1000000;
  void broke(uint64_t at) { times_[next_]=at; next_=uint8_t((next_+1)%4); if (count_<4) ++count_; }
  bool unsteady(uint64_t now) const {
    uint8_t recent=0;
    for (uint8_t i=0;i<count_;++i) if (now>=times_[i] && now-times_[i]<windowUs) ++recent;
    return recent>=2;
  }
 private:
  uint64_t times_[4]={};
  uint8_t next_=0, count_=0;
};
// fault: latched hardware/protocol fault, needs a restart.
// broken: the raw receiver says no beam right now (before any qualification).
// clear: qualified clear (20 ms steady). ready: clear and in step with ST-1.
inline LightView statusLight(bool fault,FinishState state,bool broken,bool clear,bool unsteady,
                             bool ready,uint64_t now) {
  LightView v; v.lit=true;
  const bool fast=(now/125000)%2==0, slow=(now/500000)%2==0;
  if (fault) { v.colour=Light::Red; v.lit=fast; return v; }       // Restart needed.
  switch (state) {
    case FinishState::Armed: case FinishState::Cued: case FinishState::Running:
      v.colour=Light::Blue; return v;                                // Waiting for the rider.
    case FinishState::Complete:
      v.colour=Light::White; return v;                               // Time captured.
    default: break;
  }
  if (broken) v.colour=Light::Red;                                   // No beam: aim it.
  else if (unsteady || !clear) v.colour=Light::Amber;                // Beam drops out: adjust.
  else if (!ready) { v.colour=Light::Green; v.lit=slow; }            // Beam good, waiting for ST-1.
  else v.colour=Light::Green;                                        // Ready.
  return v;
}
}
