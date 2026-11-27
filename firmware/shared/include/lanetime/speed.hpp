#pragma once
#include <stdint.h>
namespace jns {
// Whole milliseconds, rounded as every display shows them, so a time and the speed
// worked out from it always agree. 0 is never a valid time.
inline uint32_t wholeMs(uint64_t us) {
  const uint64_t ms=(us+500)/1000;
  return ms?uint32_t(ms):1;
}
// Average speed in tenths of km/h: metres / seconds * 3.6.
inline uint32_t tenthsKmh(uint16_t metres,uint32_t ms) {
  return ms?uint32_t((uint64_t(metres)*36000+ms/2)/ms):0;
}
}
