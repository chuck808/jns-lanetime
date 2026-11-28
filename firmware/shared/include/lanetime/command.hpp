#pragma once
#include <stdint.h>
namespace jns {
// Remote <-> ST-1 commands (wire v5). Kind::Command packets carry everything in value:
//   bits 0-7 code, 8-15 reply, 16-23 reason, 24-39 arg, 40-63 sequence.
// Remote -> ST-1: reply=Request. arg = milliseconds since the button was pressed
// (presses), battery percent (Hello, 255 = unknown). ST-1 -> remote: same code and
// sequence, reply Accepted or Refused with a reason; for an accepted Pair, arg = slot.
enum class Code : uint8_t { Hello=1, Arm, Go, Cancel, Reveal, TakeControl, Pair };
enum class Reply : uint8_t { Request=0, Accepted, Refused };
enum class Reason : uint8_t {
  None=0, NotPaired, NotInControl, TooLate, WrongAttempt, WrongSession, NotReady, Busy, Full, Bad
};
struct Command {
  Code code=Code::Hello;
  Reply reply=Reply::Request;
  Reason reason=Reason::None;
  uint16_t arg=0;
  uint32_t seq=0;  // 24 bits on the wire.
};
constexpr uint32_t kSeqMask=0xFFFFFF;
inline uint64_t packCommand(const Command& c) {
  return uint64_t(uint8_t(c.code)) | uint64_t(uint8_t(c.reply))<<8 | uint64_t(uint8_t(c.reason))<<16 |
    uint64_t(c.arg)<<24 | uint64_t(c.seq&kSeqMask)<<40;
}
inline bool unpackCommand(uint64_t v,Command& c) {
  const uint8_t code=uint8_t(v), reply=uint8_t(v>>8), reason=uint8_t(v>>16);
  if (code<1 || code>7 || reply>2 || reason>9) return false;
  c.code=Code(code); c.reply=Reply(reply); c.reason=Reason(reason);
  c.arg=uint16_t(v>>24); c.seq=uint32_t(v>>40)&kSeqMask;
  return true;
}
// A press older than this is refused, so a GO can never act noticeably late. CANCEL is exempt.
constexpr uint16_t kCommandMaxAgeMs=250;
inline const char* reasonName(Reason r) {
  switch (r) {
    case Reason::None: return "OK";
    case Reason::NotPaired: return "not paired";
    case Reason::NotInControl: return "not in control";
    case Reason::TooLate: return "too late";
    case Reason::WrongAttempt: return "wrong attempt";
    case Reason::WrongSession: return "ST-1 restarted";
    case Reason::NotReady: return "not ready";
    case Reason::Busy: return "attempt running";
    case Reason::Full: return "4 remotes paired";
    case Reason::Bad: return "bad command";
  }
  return "?";
}
}
