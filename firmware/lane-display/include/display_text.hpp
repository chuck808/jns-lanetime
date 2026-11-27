#pragma once
#include <stdio.h>
#include <lanetime/speed.hpp>
#include "lane_display.hpp"
namespace jns {
// At most six characters, including the decimal; 5x7 digits + narrow dot fit 32 columns.
// Times always show two digits of seconds: 01.750, 12.345, 30.000.
inline void displayText(DisplayState state,uint64_t elapsed,bool synced,char (&out)[8]) {
  const char* text="----";
  switch (state) {
    case DisplayState::Idle: text="IDLE"; break;
    case DisplayState::Disabled: text="OFF"; break;
    case DisplayState::Pending: text=synced?"----":"SYNC"; break;
    case DisplayState::Dnf: text="DNF"; break;
    case DisplayState::Unavailable: text="ERR"; break;
    case DisplayState::Fault: text="FAIL"; break;
    case DisplayState::Time: {
      if (!elapsed || elapsed>30000000) { text="ERR"; break; }
      const uint32_t ms=wholeMs(elapsed);
      snprintf(out,sizeof(out),"%02u.%03u",unsigned(ms/1000)%100u,unsigned(ms%1000));
      return;
    }
    case DisplayState::Offline: break;
  }
  snprintf(out,sizeof(out),"%s",text);
}
// Average speed for a shown time, e.g. "54.0 KM/H". False when there is nothing to show:
// no time, or ST-1 had no distance set.
inline bool speedText(DisplayState state,uint64_t elapsed,uint8_t metres,char (&out)[16]) {
  if (state!=DisplayState::Time || !metres || !elapsed || elapsed>30000000) return false;
  const uint32_t kmh=tenthsKmh(metres,wholeMs(elapsed));
  if (kmh>=10000) return false;  // 1000 km/h or more: not a real ride.
  snprintf(out,sizeof(out),"%u.%u KM/H",unsigned(kmh/10)%1000u,unsigned(kmh%10));
  return true;
}
// A revealed time is held still, then the speed scrolls across once, and the cycle repeats.
// scroll=false: show the time. scroll=true: the speed text's left edge is at column left
// (32 = just off the right-hand edge, negative = partly off the left).
struct Frame { bool scroll=false; int16_t left=0; };
constexpr uint64_t kHoldUs=4000000, kColumnUs=40000;  // 25 columns a second.
inline Frame resultFrame(uint64_t sinceReveal,uint16_t speedColumns,bool haveSpeed) {
  Frame f;
  if (!haveSpeed || !speedColumns) return f;
  const uint64_t travel=uint64_t(speedColumns)+32;
  const uint64_t t=sinceReveal%(kHoldUs+travel*kColumnUs);
  if (t<kHoldUs) return f;
  f.scroll=true; f.left=int16_t(32-int64_t((t-kHoldUs)/kColumnUs));
  return f;
}
}
