#include <Arduino.h>
#include "app_config.h"
#include "shared.h"
#include "io.h"
#include "buttons.h"
#include "control.h"

/*
  Boot sequence:
  - Bring up serial at high baud for snappy logs
  - Init shared queue + I/O + buttons
  - Start the 500 Hz control task on Core 1
  - Keep loop() idle (we’ll add web server later on Core 0)
*/

void setup() {
  Serial.begin(921600);
  delay(150);
  Serial.println("\n[BOOT] VSU-ESP32 starting…");

  shared_init();
  io_init();
  buttons_init();

  // Defaults: paused=1, power=30, mode=FWD, bpm=5.0 (set in Shared)
  control_start_task();
}

void loop() {
  // Keep idle; control is its own FreeRTOS task.
  vTaskDelay(pdMS_TO_TICKS(200));
}
