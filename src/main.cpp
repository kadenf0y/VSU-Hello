#include <Arduino.h>
#include "app_config.h"
#include <io.h>
#include <control.h>
#include <buttons.h>

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[HELLO] ESP32 + io + control + buttons.");
  io_init();
  buttons_init();
  buttons_debug_print();   // one-shot pin sanity line
  control_start_task();
}

void loop() {                 // <-- must be EXACTLY 'void loop()' at global scope
  static uint32_t t = 0;
  if (millis() - t >= 1000) {
    t = millis();
    Serial.println("[HELLO] tick");
  }
}
