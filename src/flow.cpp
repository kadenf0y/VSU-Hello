#include <Arduino.h>
#include "shared.h"
#include "app_config.h"
#include "flow.h"

static volatile uint32_t s_pulses = 0;
static volatile uint32_t s_lastMicros = 0;  // simple deglitch (µs)

// Optional: keep last computed stats for debug
static volatile uint32_t s_lastWindowMs = FLOW_WINDOW_MS_MAX;
static volatile uint32_t s_lastCnt = 0;
static volatile float    s_lastHz = 0.0f;

static void IRAM_ATTR flow_isr(){
  uint32_t now = micros();
  // Drop glitches faster than 100 µs (~10 kHz limit); adjust if needed
  if (now - s_lastMicros >= 100) {
    s_pulses++;
    s_lastMicros = now;
  }
}

static inline uint32_t clampU32(uint32_t v, uint32_t lo, uint32_t hi){
  return v < lo ? lo : (v > hi ? hi : v);
}

static void flow_task(void*){
  uint32_t window_ms = FLOW_WINDOW_MS_MAX;     // start slow for low Hz
  uint32_t t0 = millis();

  for(;;){
    uint32_t now = millis();
    if (now - t0 >= window_ms){
      uint32_t elapsed = now - t0;
      t0 = now;

      // snapshot & reset counter
      noInterrupts();
      uint32_t cnt = s_pulses;
      s_pulses = 0;
      interrupts();

      float elapsed_s = elapsed / 1000.0f;
      float hz = (elapsed_s > 0.f) ? (cnt / elapsed_s) : 0.f;

      // publish flow in mL/min for SSE/UI
      float q_ml_min = flow_hz_to_mlmin(hz);
      G.flow_ml_min.store(q_ml_min, std::memory_order_relaxed);

      // keep debug stats
      s_lastWindowMs = elapsed;
      s_lastCnt      = cnt;
      s_lastHz       = hz;

      // choose next window targeting ~FLOW_TARGET_PULSES
      if (hz > 0.f){
        uint32_t next_ms = (uint32_t) lroundf((FLOW_TARGET_PULSES / hz) * 1000.f);
        window_ms = clampU32(next_ms, FLOW_WINDOW_MS_MIN, FLOW_WINDOW_MS_MAX);
      }else{
        window_ms = FLOW_WINDOW_MS_MAX;
      }
    }

    // small sleep to yield CPU; no need to block until end of window
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void flow_begin(){
  // Pin bias is external (divider or level shifter); no internal pull-ups
  pinMode(PIN_FLOW, INPUT);

  attachInterrupt(
    digitalPinToInterrupt(PIN_FLOW),
    flow_isr,
    FLOW_EDGE_FALLING ? FALLING : RISING
  );

  // Background task on APP core (or change to your preferred core)
  xTaskCreatePinnedToCore(flow_task, "flow", 2048, nullptr, 3, nullptr, APP_CPU_NUM);
}

// (Optional) Tiny inline debug helper if you want to read stats elsewhere:
//
// static inline void flow_dbg(uint32_t& ms, uint32_t& cnt, float& hz){
//   ms = s_lastWindowMs; cnt = s_lastCnt; hz = s_lastHz;
// }
