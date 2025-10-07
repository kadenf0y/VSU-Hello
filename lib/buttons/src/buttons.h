#pragma once
#include <Arduino.h>
#include "app_config.h"

// Call once at boot
void buttons_init();

// Debounced snapshot + one-shot edges (reset each call)
struct BtnState {
  bool aPressed;
  bool bPressed;
  bool aPressEdge;
  bool aReleaseEdge;
  bool bPressEdge;
  bool bReleaseEdge;
};

// Call once per control loop iteration (or as often as you like).
// Edges are true only on the call where the debounced transition occurs.
void buttons_read_debounced(BtnState &out);

// Optional one-shot debug print (raw pin levels)
void buttons_debug_print();
