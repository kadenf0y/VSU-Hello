#include <Arduino.h>
#include "app_config.h"
#include <buttons.h>
#include "driver/gpio.h"   // belt & suspenders for pullups

void buttons_init() {
  pinMode(PIN_BTN_A, INPUT_PULLUP);
  pinMode(PIN_BTN_B, INPUT_PULLUP);

  // Make sure pull-ups are really on, pulldowns off
  gpio_pullup_en( (gpio_num_t)PIN_BTN_A );
  gpio_pulldown_dis( (gpio_num_t)PIN_BTN_A );
  gpio_pullup_en( (gpio_num_t)PIN_BTN_B );
  gpio_pulldown_dis( (gpio_num_t)PIN_BTN_B );
}

void buttons_sample(bool &aPressed, bool &bPressed) {
  // Active-LOW: LOW means "pressed"
  aPressed = (digitalRead(PIN_BTN_A) == LOW);
  bPressed = (digitalRead(PIN_BTN_B) == LOW);
}

// --- optional: one-shot debug line to inspect raw logic levels
void buttons_debug_print() {
  int rawA = digitalRead(PIN_BTN_A);
  int rawB = digitalRead(PIN_BTN_B);
  Serial.printf("[BTN] pins: A=%d raw=%d  B=%d raw=%d  (1≈3.3V, 0≈0V)\n",
                PIN_BTN_A, rawA, PIN_BTN_B, rawB);
}
