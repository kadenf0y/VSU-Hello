#pragma once
#include <atomic>
#include "app_config.h"
extern "C" {
  #include "freertos/FreeRTOS.h"
  #include "freertos/queue.h"
}

// ---- command types ----
enum CmdType : uint8_t {
  CMD_TOGGLE_PAUSE = 0,
  CMD_SET_MODE     = 1,   // arg: iarg = 0=fwd,1=rev,2=beat
  CMD_SET_POWER_PCT= 2,   // arg: iarg = 10..100 (ABSOLUTE target)
  CMD_SET_BPM      = 3
};

struct Cmd { CmdType type; int32_t iarg; float farg; };

// queue API
void          shared_cmdq_init();
QueueHandle_t shared_cmdq();
bool          shared_cmd_post(const Cmd& c);

// ---- telemetry/control state ----
struct SharedState {
  std::atomic<float>   vent_mmHg{0};
  std::atomic<float>   atr_mmHg{0};
  std::atomic<float>   flow_ml_min{0};

  std::atomic<unsigned> pwm{0};     // instantaneous PWM (0..255), for telemetry
  std::atomic<unsigned> valve{0};   // 0/1

  std::atomic<int>     mode{0};     // 0=fwd,1=rev,2=beat (placeholder)
  std::atomic<int>     paused{0};   // 0=run,1=paused
  std::atomic<int>     powerPct{100}; // 10..100, shared global
  std::atomic<float>   bpm{5.0f};     // default 5 as you wanted
  std::atomic<float>   loopMs{0};
};

extern SharedState G;
