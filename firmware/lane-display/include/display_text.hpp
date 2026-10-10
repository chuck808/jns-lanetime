#pragma once
#include <stdio.h>
#include "lane_display.hpp"
namespace jns {
// At most six characters, including the decimal; 5x7 digits + narrow dot fit 32 columns.
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
      const uint32_t ms=uint32_t((elapsed+500)/1000);
      snprintf(out,sizeof(out),"%lu.%03lu",(unsigned long)(ms/1000),(unsigned long)(ms%1000));
      return;
    }
    case DisplayState::Offline: break;
  }
  snprintf(out,sizeof(out),"%s",text);
}
}
