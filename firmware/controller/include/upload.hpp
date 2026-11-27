#pragma once
#include <stdint.h>
#include "efforts.hpp"
namespace jns {
struct UploadSummary {
  bool configured=false, connected=false;
  uint8_t saved=0, rejected=0, left=0;
  char stopCode[28]={};  // Why the round stopped early, if it did.
};
// Joins the configured Wi-Fi network, sends every pending effort to JNS SPLIT, then
// disconnects. ESP-NOW must already be stopped; the caller restarts it afterwards.
UploadSummary uploadPending(EffortQueue& queue);
}
