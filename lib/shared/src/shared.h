#pragma once
#include <atomic>
#include "app_config.h"
extern "C" {
  #include "freertos/FreeRTOS.h"
  #include "freertos/queue.h"
}

// ---- command types (extend later) ----
enum CmdType : uint8_t {
  CMD_TOGGLE_PAUSE = 0,
  CMD_SET_MODE     = 1,   // arg: iarg = 0=fwd,1=rev,2=beat
  CMD_SET_POWER_PCT= 2,   // arg: iarg = 10..100
  CMD_SET_BPM      = 3    // arg: farg = bpm
};

struct Cmd {
  CmdType  type;
  int32_t  iarg;   // integer payload (mode, power)
  float    farg;   // float payload   (bpm)
};

// queue API
void          shared_cmdq_init();
QueueHandle_t shared_cmdq();                 // returns handle (or nullptr)
bool          shared_cmd_post(const Cmd& c); // non-blocking post

// ---- telemetry state (simple for now) ----
struct SharedState {
  std::atomic<float>   vent_mmHg{0};
  std::atomic<float>   atr_mmHg{0};
  std::atomic<float>   flow_ml_min{0};
  std::atomic<unsigned> pwm{0};
  std::atomic<unsigned> valve{0};
  std::atomic<int>     mode{0};
  std::atomic<int>     paused{0};
  std::atomic<float>   bpm{0};
  std::atomic<float>   loopMs{0};
};

extern SharedState G;
