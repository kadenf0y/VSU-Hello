#include <Arduino.h>
#include "app_config.h"
#include <io.h>
#include <control.h>
#include <buttons.h>
#include <shared.h>        // <-- make sure this is included

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[HELLO] ESP32 + io + control + buttons + cmdq.");

  io_init();
  buttons_init();

  shared_cmdq_init();      // <-- CREATE THE QUEUE
  Serial.printf("[MAIN] cmdq handle = %p\n", (void*)shared_cmdq());

  control_start_task();
}

void loop() {
  static uint32_t t = 0;
  if (millis() - t >= 200) {
    t = millis();
  }
}
