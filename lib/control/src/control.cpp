#include <Arduino.h>
#include "app_config.h"
#include <control.h>
#include <io.h>
#include <shared.h>
#include <buttons.h>

static inline float countsToMMHg(uint16_t c) { return 0.4766825f * c - 204.409f; }

// Fallback: if queue is missing or full, apply command immediately
static void applyCmdInline(const Cmd& cmd) {
  switch (cmd.type) {
    case CMD_TOGGLE_PAUSE: {
      int p = G.paused.load(std::memory_order_relaxed);
      p = p ? 0 : 1;
      G.paused.store(p, std::memory_order_relaxed);
      Serial.printf("[CTRL] (inline) TOGGLE_PAUSE -> paused=%d\n", p);
    } break;

    case CMD_SET_POWER_PCT: {
      int pct = cmd.iarg; if (pct < 10) pct = 10; if (pct > 100) pct = 100;
      G.powerPct.store(pct, std::memory_order_relaxed);
      Serial.printf("[CTRL] (inline) SET_POWER_PCT -> %d%%\n", pct);
    } break;

    case CMD_SET_MODE:
      G.mode.store(cmd.iarg, std::memory_order_relaxed);
      Serial.printf("[CTRL] (inline) SET_MODE -> %d\n", cmd.iarg);
      break;

    case CMD_SET_BPM:
      G.bpm.store(cmd.farg, std::memory_order_relaxed);
      Serial.printf("[CTRL] (inline) SET_BPM -> %.1f\n", cmd.farg);
      break;
  }
}

static void taskControl(void*) {
  const TickType_t period = pdMS_TO_TICKS(1000 / CONTROL_HZ);
  TickType_t wake = xTaskGetTickCount();

  pinMode(PIN_LED, OUTPUT);
  bool led = false;
  uint32_t lastBlinkMs = millis();

  float loopMsEMA = 1000.0f / CONTROL_HZ;
  uint32_t lastLoopStartUs = micros();
  uint32_t lastPrintMs = millis();

  // Button B long/short timing
  uint32_t bPressMs = 0;
  bool bLongFired = false;
  const uint32_t LONG_MS = 500;

  for (;;) {
    // --- loop timing ---
    uint32_t nowUs = micros();
    float thisMs = (nowUs - lastLoopStartUs) * 0.001f;
    lastLoopStartUs = nowUs;
    loopMsEMA = loopMsEMA * 0.9f + thisMs * 0.1f;
    G.loopMs.store(loopMsEMA, std::memory_order_relaxed);

    // --- debounced buttons ---
    BtnState ev{};
    buttons_read_debounced(ev);

    // A: pressed edge -> toggle pause
    if (ev.aPressEdge) {
      Cmd c{ CMD_TOGGLE_PAUSE, 0, 0.0f };
      if (!shared_cmd_post(c)) applyCmdInline(c);
    }

    // B: long/short using debounced edges/state
    if (ev.bPressEdge) { bPressMs = millis(); bLongFired = false; }
    if (ev.bPressed && !bLongFired) {
      if (millis() - bPressMs >= LONG_MS) {
        int cur = G.powerPct.load(std::memory_order_relaxed);
        int target = cur - 10; if (target < 10) target = 100;
        Cmd c{ CMD_SET_POWER_PCT, target, 0.0f };
        if (!shared_cmd_post(c)) applyCmdInline(c);
        bLongFired = true;
      }
    }
    if (ev.bReleaseEdge && !bLongFired) {
      int cur = G.powerPct.load(std::memory_order_relaxed);
      int target = cur + 10; if (target > 100) target = 10;
      Cmd c{ CMD_SET_POWER_PCT, target, 0.0f };
      if (!shared_cmd_post(c)) applyCmdInline(c);
    }

    // --- consume any queued commands (non-blocking) ---
    Cmd cmd;
    while (shared_cmdq() && xQueueReceive(shared_cmdq(), &cmd, 0) == pdTRUE) {
      switch (cmd.type) {
        case CMD_TOGGLE_PAUSE: {
          int p = G.paused.load(std::memory_order_relaxed);
          p = p ? 0 : 1;
          G.paused.store(p, std::memory_order_relaxed);
          Serial.printf("[CTRL] cmd: TOGGLE_PAUSE -> paused=%d\n", p);
        } break;

        case CMD_SET_POWER_PCT: {
          int pct = cmd.iarg; if (pct < 10) pct = 10; if (pct > 100) pct = 100;
          G.powerPct.store(pct, std::memory_order_relaxed);
          Serial.printf("[CTRL] cmd: SET_POWER_PCT -> %d%%\n", pct);
        } break;

        case CMD_SET_MODE:
          G.mode.store(cmd.iarg, std::memory_order_relaxed);
          Serial.printf("[CTRL] cmd: SET_MODE -> %d\n", cmd.iarg);
          break;

        case CMD_SET_BPM:
          G.bpm.store(cmd.farg, std::memory_order_relaxed);
          Serial.printf("[CTRL] cmd: SET_BPM -> %.1f\n", cmd.farg);
          break;
      }
    }

    // --- sensors (still safe; no actuation yet) ---
    uint16_t rVent = io_adc_read_vent();
    uint16_t rAtr  = io_adc_read_atr();
    float v_mm = countsToMMHg(rVent);
    float a_mm = countsToMMHg(rAtr);
    G.vent_mmHg.store(v_mm, std::memory_order_relaxed);
    G.atr_mmHg.store(a_mm,  std::memory_order_relaxed);
    G.flow_ml_min.store(0.0f, std::memory_order_relaxed);

    // no actuation yet; publish placeholders
    G.pwm.store(0, std::memory_order_relaxed);
    G.valve.store(0, std::memory_order_relaxed);

    // --- once/sec status line ---
    uint32_t nowMs = millis();
    if (nowMs - lastPrintMs >= 1000) {
      lastPrintMs = nowMs;
      float hz = 1000.0f / (loopMsEMA > 0 ? loopMsEMA : 1);
      Serial.printf("[CTRL] Hz=%.1f Ms=%.3f  A=%d B=%d  paused=%d  power=%d%%\n",
                    hz, loopMsEMA,
                    ev.aPressed?1:0, ev.bPressed?1:0,
                    G.paused.load(), G.powerPct.load());
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
  // Core 1 = PRO_CPU
  xTaskCreatePinnedToCore(taskControl, "control", 4096, nullptr, 4, nullptr, 1);
}
