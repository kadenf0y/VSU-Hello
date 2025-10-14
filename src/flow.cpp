// src/flow.cpp
#include <Arduino.h>
#include "shared.h"
#include "app_config.h"
#include "flow.h"

// ---- DEBUG SWITCH: set to 1 to print raw pin level, pending pulses, and per-window stats
#define FLOW_DEBUG 1

#ifndef APP_CPU_NUM
// Fallback: run the task on core 1 if your project doesn't define APP_CPU_NUM
#define APP_CPU_NUM 1
#endif

// ---- ISR-shared counters ----
static volatile uint32_t s_pulses      = 0;                  // pulses accumulated in current window
static volatile uint32_t s_lastMicros  = 0;                  // for simple deglitch (µs)

// ---- Last computed stats (handy for debug/inspection) ----
static volatile uint32_t s_lastWindowMs = FLOW_WINDOW_MS_MAX;
static volatile uint32_t s_lastCnt      = 0;
static volatile float    s_lastHz       = 0.0f;

// -----------------------------------------------------------------------------
// Interrupt Service Routine: count edges, ignore glitches faster than 100 µs
// -----------------------------------------------------------------------------
static void IRAM_ATTR flow_isr() {
  uint32_t now = micros();
  if ((now - s_lastMicros) >= 100) {    // ~10 kHz max edge rate
    s_pulses++;
    s_lastMicros = now;
  }
}

// -----------------------------------------------------------------------------
// Small utility
// -----------------------------------------------------------------------------
static inline uint32_t clampU32(uint32_t v, uint32_t lo, uint32_t hi) {
  return (v < lo) ? lo : ((v > hi) ? hi : v);
}

// -----------------------------------------------------------------------------
// Background task: dynamically sized windows, compute Hz and mL/min, publish to G
// Also prints periodic debug when FLOW_DEBUG=1
// -----------------------------------------------------------------------------
static void flow_task(void*) {
  uint32_t window_ms = FLOW_WINDOW_MS_MAX;   // start wide for low rates
  uint32_t t0 = millis();

#if FLOW_DEBUG
  uint32_t dbgT = 0;
#endif

  for (;;) {
    uint32_t now = millis();

#if FLOW_DEBUG
    // Every ~200 ms: show current GPIO level and the live (pending) pulse accumulator
    if ((now - dbgT) >= 200) {
      dbgT = now;
      int lvl = digitalRead(PIN_FLOW);
      noInterrupts();
      uint32_t pending = s_pulses;
      interrupts();
      Serial.printf("[flow] pin=%d lvl=%d pending=%lu window=%lums\n",
                    PIN_FLOW, lvl, (unsigned long)pending, (unsigned long)window_ms);
    }
#endif

    // When the window elapses: compute, publish, and choose the next window
    if ((now - t0) >= window_ms) {
      uint32_t elapsed = now - t0;
      t0 = now;

      // Snapshot & reset pulse counter
      noInterrupts();
      uint32_t cnt = s_pulses;
      s_pulses = 0;
      interrupts();

      float elapsed_s = elapsed / 1000.0f;
      float hz        = (elapsed_s > 0.0f) ? (cnt / elapsed_s) : 0.0f;
      float q_ml_min  = hz * FLOW_HZ_TO_ML_MIN;  // conversion set in app_config.h

      // Publish to global atomics (read by SSE / UI)
      G.flow_ml_min.store(q_ml_min, std::memory_order_relaxed);

      // Keep debug stats
      s_lastWindowMs = elapsed;
      s_lastCnt      = cnt;
      s_lastHz       = hz;

#if FLOW_DEBUG
      Serial.printf("[flow] window=%lums cnt=%lu hz=%.3f q=%.1f\n",
                    (unsigned long)elapsed, (unsigned long)cnt, hz, q_ml_min);
#endif

      // Choose next window to target ~FLOW_TARGET_PULSES pulses
      if (hz > 0.0f) {
        uint32_t next_ms = (uint32_t) lroundf((FLOW_TARGET_PULSES / hz) * 1000.0f);
        window_ms = clampU32(next_ms, FLOW_WINDOW_MS_MIN, FLOW_WINDOW_MS_MAX);
      } else {
        window_ms = FLOW_WINDOW_MS_MAX;  // no pulses → widen window
      }
    }

    // Tiny yield so we’re not a busy loop
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// -----------------------------------------------------------------------------
// Public entry: call once at startup
// -----------------------------------------------------------------------------
void flow_begin() {
  // Pin is biased by your hardware (divider/level shifter). No internal pull-up.
  pinMode(PIN_FLOW, INPUT);

  attachInterrupt(
    digitalPinToInterrupt(PIN_FLOW),
    flow_isr,
    FLOW_EDGE_FALLING ? FALLING : RISING
  );

#if FLOW_DEBUG
  Serial.printf("[flow] begin (pin=%d, edge=%s)\n",
                PIN_FLOW, FLOW_EDGE_FALLING ? "FALLING" : "RISING");
#endif

  // Run the sampler task on the chosen core
  xTaskCreatePinnedToCore(flow_task, "flow", 2048, nullptr, 3, nullptr, APP_CPU_NUM);
}
