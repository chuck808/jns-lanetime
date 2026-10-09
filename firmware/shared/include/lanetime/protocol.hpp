#pragma once
#include <stddef.h>
#include <stdint.h>
namespace jns {
enum class Kind : uint8_t { Status=1, Arm, Cancel, Go, Start, Result, Reveal };
enum class Role : uint8_t { Controller=1, Start, Finish };
enum class Mode : uint8_t { Flying=0, Standing=1 };
enum Flag : uint8_t { Ready=1, Armed=2, Running=4, Complete=8, Fault=16, Cued=32 };
// value: controller Status = ST-1 clock at submission (common timebase);
// Start = qualified beam event in ST-1 time; Go = scheduled GO in ST-1 time;
// Result = elapsed microseconds; peer Status with Cued = the GO time it holds;
// Reveal = scheduled reveal time in ST-1 time, with finishedMask in byte 27.
struct Packet {
  Kind kind=Kind::Status;
  Role role=Role::Controller;
  uint8_t lane=1;
  uint32_t system=0;
  uint64_t session=0;
  uint32_t attempt=0;
  Mode mode=Mode::Flying;
  uint8_t mask=1, flags=0, finishedMask=0;
  uint64_t value=0;
  uint32_t boot=0;
};
constexpr size_t kPacketSize=40;
constexpr uint8_t kVersion=2;
inline void put(uint8_t* b, uint64_t v, size_t n) {
  for (size_t i=0; i<n; ++i) b[i]=uint8_t(v>>(8*i));
}
inline uint64_t get(const uint8_t* b, size_t n) {
  uint64_t v=0;
  for (size_t i=0; i<n; ++i) v|=uint64_t(b[i])<<(8*i);
  return v;
}
inline void encode(const Packet& p, uint8_t* b) {
  b[0]='J'; b[1]='N'; b[2]='S'; b[3]='L'; b[4]=kVersion;
  b[5]=uint8_t(p.kind); b[6]=uint8_t(p.role); b[7]=p.lane;
  put(b+8,p.system,4); put(b+12,p.session,8); put(b+20,p.attempt,4);
  b[24]=uint8_t(p.mode); b[25]=p.mask; b[26]=p.flags; b[27]=p.finishedMask;
  put(b+28,p.value,8); put(b+36,p.boot,4);
}
inline bool decode(const uint8_t* b, size_t n, Packet& p) {
  if (n!=kPacketSize || b[0]!='J' || b[1]!='N' || b[2]!='S' || b[3]!='L' ||
      b[4]!=kVersion || b[5]<1 || b[5]>7 || b[6]<1 || b[6]>3 || b[7]<1 || b[7]>4 ||
      b[24]>1 || !b[25] || b[25]>15 || (b[26]&~63)) return false;
  p.kind=Kind(b[5]); p.role=Role(b[6]); p.lane=b[7];
  if ((p.role==Role::Controller && p.lane!=1) ||
      ((p.kind==Kind::Arm || p.kind==Kind::Cancel || p.kind==Kind::Go || p.kind==Kind::Reveal) && p.role!=Role::Controller) ||
      (p.kind==Kind::Start && p.role==Role::Finish) ||
      (p.kind==Kind::Result && p.role!=Role::Finish)) return false;
  p.system=uint32_t(get(b+8,4)); p.session=get(b+12,8); p.attempt=uint32_t(get(b+20,4));
  p.mode=Mode(b[24]); p.mask=b[25]; p.flags=b[26]; p.finishedMask=b[27];
  p.value=get(b+28,8); p.boot=uint32_t(get(b+36,4));
  if (p.kind==Kind::Reveal) {
    if ((p.finishedMask&~p.mask) || !p.session || !p.attempt || !p.value) return false;
  } else if (p.finishedMask) return false;
  return p.boot!=0;
}
}
