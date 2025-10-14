// web_cal.h
#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// ---- Hooks so web_cal.cpp doesn't depend on your internals ----
// Provide raw sensor readers and low-level actuators from your app.
struct WebCalHooks {
  float (*readAtrRawOnce)();   // return current *raw* atrium reading
  float (*readVentRawOnce)();  // return current *raw* ventricle reading
  int   (*getValveDir)();      // -1, 0, +1
  void  (*setValveDir)(int dir);
  void  (*setPwmRaw)(uint8_t pwm); // 0..255
};

// Call this once to provide the function pointers from your app.
void web_cal_set_hooks(const WebCalHooks& hooks);

// Register all /cal and /api/cal/* routes on the given AsyncWebServer.
void web_cal_register_routes(AsyncWebServer& server);
