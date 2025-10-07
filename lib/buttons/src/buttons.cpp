#include <Arduino.h>
#include "app_config.h"
#include <buttons.h>
#include "driver/gpio.h"

static const uint32_t DEBOUNCE_MS = 30;  // 30 ms debounce

struct Deb {
  bool raw;            // instantaneous raw (active-LOW -> true when LOW)
  bool stable;         // debounced state
  uint32_t lastFlip;   // last time raw changed
  bool inited;
};

static Deb dA{}, dB{};

static inline bool readRawPressed(int pin) {
  // active-LOW: pressed when pin reads LOW
  return digitalRead(pin) == LOW;
}

void buttons_init() {
  pinMode(PIN_BTN_A, INPUT_PULLUP);
  pinMode(PIN_BTN_B, INPUT_PULLUP);
  // belt & suspenders to ensure pull-ups are on
  gpio_pullup_en( (gpio_num_t)PIN_BTN_A ); gpio_pulldown_dis( (gpio_num_t)PIN_BTN_A );
  gpio_pullup_en( (gpio_num_t)PIN_BTN_B ); gpio_pulldown_dis( (gpio_num_t)PIN_BTN_B );

  uint32_t now = millis();
  dA.raw = dA.stable = readRawPressed(PIN_BTN_A);
  dA.lastFlip = now; dA.inited = true;

  dB.raw = dB.stable = readRawPressed(PIN_BTN_B);
  dB.lastFlip = now; dB.inited = true;
}

static inline void processDebounce(Deb &d, bool newRaw, uint32_t now,
                                   bool &pressed, bool &pressEdge, bool &releaseEdge) {
  pressEdge = releaseEdge = false;
  if (!d.inited) { d.raw = d.stable = newRaw; d.lastFlip = now; d.inited = true; }

  if (newRaw != d.raw) { d.raw = newRaw; d.lastFlip = now; }

  if (d.stable != d.raw && (now - d.lastFlip) >= DEBOUNCE_MS) {
    bool old = d.stable;
    d.stable = d.raw;
    if (d.stable && !old) pressEdge   = true;  // became pressed
    if (!d.stable && old) releaseEdge = true;  // became released
  }
  pressed = d.stable;
}

void buttons_read_debounced(BtnState &out) {
  uint32_t now = millis();
  bool rawA = readRawPressed(PIN_BTN_A);
  bool rawB = readRawPressed(PIN_BTN_B);
  processDebounce(dA, rawA, now, out.aPressed, out.aPressEdge, out.aReleaseEdge);
  processDebounce(dB, rawB, now, out.bPressed, out.bPressEdge, out.bReleaseEdge);
}

void buttons_debug_print() {
  int rawA = digitalRead(PIN_BTN_A);
  int rawB = digitalRead(PIN_BTN_B);
  Serial.printf("[BTN] pins: A=%d raw=%d  B=%d raw=%d  (1≈3.3V, 0≈0V)\n",
                PIN_BTN_A, rawA, PIN_BTN_B, rawB);
}
