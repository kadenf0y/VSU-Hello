#include <Arduino.h>
#include "app_config.h"
#include <control.h>
#include <io.h>
#include <shared.h>
#include <buttons.h>

// counts -> mmHg
static inline float countsToMMHg(uint16_t c) { return 0.4766825f * c - 204.409f; }

static void taskControl(void*) {
  const TickType_t period = pdMS_TO_TICKS(1000 / CONTROL_HZ);
  TickType_t wake = xTaskGetTickCount();

  pinMode(PIN_LED, OUTPUT);
  bool led = false;
  uint32_t lastBlinkMs = millis();

  float loopMsEMA = 1000.0f / CONTROL_HZ;
  uint32_t lastLoopStartUs = micros();
  uint32_t lastPrintMs = millis();

  // button edge tracking
  bool lastA = false, lastB = false;

  for (;;) {
    // timing
    uint32_t nowUs = micros();
    float thisMs = (nowUs - lastLoopStartUs) * 0.001f;
    lastLoopStartUs = nowUs;
    loopMsEMA = loopMsEMA * 0.9f + thisMs * 0.1f;
    G.loopMs.store(loopMsEMA, std::memory_order_relaxed);

    // sample buttons (pressed = true when LOW)
    bool a=false, b=false;
    buttons_sample(a, b);

    // on A pressed-edge -> post toggle pause
    if (a && !lastA) {
      Cmd c{ CMD_TOGGLE_PAUSE, 0, 0.0f };
      shared_cmd_post(c);
    }
    lastA = a; lastB = b;

    // consume any queued commands (non-blocking)
    Cmd cmd;
    while (shared_cmdq() && xQueueReceive(shared_cmdq(), &cmd, 0) == pdTRUE) {
      switch (cmd.type) {
        case CMD_TOGGLE_PAUSE: {
          int p = G.paused.load(std::memory_order_relaxed);
          p = p ? 0 : 1;
          G.paused.store(p, std::memory_order_relaxed);
          Serial.printf("[CTRL] cmd: TOGGLE_PAUSE -> paused=%d\n", p);
        } break;

        case CMD_SET_MODE:
          G.mode.store(cmd.iarg, std::memory_order_relaxed);
          Serial.printf("[CTRL] cmd: SET_MODE -> %d\n", cmd.iarg);
          break;

        case CMD_SET_POWER_PCT:
          // future use
          Serial.printf("[CTRL] cmd: SET_POWER_PCT -> %d\n", cmd.iarg);
          break;

        case CMD_SET_BPM:
          // future use
          Serial.printf("[CTRL] cmd: SET_BPM -> %.1f\n", cmd.farg);
          break;
      }
    }

    // safe: read sensors (still not actuating hardware)
    uint16_t rVent = io_adc_read_vent();
    uint16_t rAtr  = io_adc_read_atr();
    float v_mm = countsToMMHg(rVent);
    float a_mm = countsToMMHg(rAtr);
    G.vent_mmHg.store(v_mm, std::memory_order_relaxed);
    G.atr_mmHg.store(a_mm,  std::memory_order_relaxed);
    G.flow_ml_min.store(0.0f, std::memory_order_relaxed);
    G.pwm.store(0, std::memory_order_relaxed);
    G.valve.store(0, std::memory_order_relaxed);

    // once/sec print
    uint32_t nowMs = millis();
    if (nowMs - lastPrintMs >= 1000) {
      lastPrintMs = nowMs;
      float hz = 1000.0f / (loopMsEMA > 0 ? loopMsEMA : 1);
      Serial.printf("[CTRL] loopHz=%.1f  loopMs=%.3f  BTN A=%d B=%d  paused=%d\n",
                    hz, loopMsEMA, a?1:0, b?1:0, G.paused.load());
    }

    // blink LED ~1 Hz
    if (nowMs - lastBlinkMs >= 1000) {
      lastBlinkMs = nowMs;
      led = !led;
      digitalWrite(PIN_LED, led ? HIGH : LOW);
    }

    vTaskDelayUntil(&wake, period);
  }
}

void control_start_task() {
  xTaskCreatePinnedToCore(taskControl, "control", 4096, nullptr, 4, nullptr, 1);
}
