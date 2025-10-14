#pragma once
#include <Arduino.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "app_config.h"

/*
  Shared state & command queue, safe across cores/tasks.
  - Atomics for sampled telemetry + current commands.
  - FreeRTOS queue for discrete command messages.
*/

enum CmdType : uint8_t {
  CMD_TOGGLE_PAUSE = 1,
  CMD_SET_POWER_PCT,
  CMD_SET_MODE,
  CMD_SET_BPM
};

struct Cmd {
  CmdType type;
  int     iarg;    // int payload (power pct, mode)
  float   farg;    // float payload (BPM)
};

struct Shared {
  // Commands / params
  std::atomic<int>    paused{1};          // 1=paused, 0=running
  std::atomic<int>    powerPct{30};       // 10..100 (maps to PWM_FLOOR..PWM_MAX)
  std::atomic<int>    mode{0};            // 0=FWD, 1=REV, 2=BEAT
  std::atomic<float>  bpm{5.0f};          // trapezoid half-cycle rate

  // Outputs we compute
  std::atomic<unsigned> pwm{0};           // 0..255
  std::atomic<unsigned> valve{0};         // 1=forward, 0=reverse

  // Telemetry (smoothed in browser later)
  std::atomic<float>  vent_mmHg{0.0f};
  std::atomic<float>  atr_mmHg{0.0f};
  std::atomic<float>  flow_ml_min{0.0f};

  // Control-loop performance
  std::atomic<float>  loopMs{2.0f};       // EMA of loop time in ms

  // ==== Scaling helpers ====
  inline float scale_atrium(float raw)   { return raw * CAL_ATR.gain  + CAL_ATR.offset; }
  inline float scale_vent(float raw)     { return raw * CAL_VENT.gain + CAL_VENT.offset; }

};

extern Shared G;

// Init queue + atomics (call once at boot)
void shared_init();

// Get the underlying command queue
QueueHandle_t shared_cmdq();

// Post a command (non-blocking, returns false if queue full/uninit)
bool          shared_cmd_post(const Cmd& c);
