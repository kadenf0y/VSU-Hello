#pragma once
#include <Arduino.h>
#include "app_config.h"

// Debounced, edge-aware button reader for two buttons.

void buttons_init();

struct BtnState {
  bool aPressed;      // true while A physically held
  bool bPressed;      // true while B physically held
  bool aPressEdge;    // one-shot true exactly on release->press
  bool aReleaseEdge;  // one-shot true exactly on press->release
  bool bPressEdge;
  bool bReleaseEdge;
};

// Call this from your control loop (500 Hz is fine).
void buttons_read_debounced(BtnState &out);

// Optional: print raw pin levels once for debugging wiring.
void buttons_debug_print();
