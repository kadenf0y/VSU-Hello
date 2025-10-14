#include <Arduino.h>
#include "app_config.h"
#include "shared.h"
#include "io.h"
#include "buttons.h"
#include "control.h"
#include "web.h"        
#include "flow.h"

void setup() {
  Serial.begin(921600);
  delay(150);
  Serial.println("\n[BOOT] VSU-ESP32 starting…");

  shared_init();
  io_init();
  buttons_init();

  flow_begin();

  control_start_task();   // core 1
  web_start();            // core 0
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(200));
}
