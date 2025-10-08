#include <Arduino.h>
#include "app_config.h"
#include <control.h>
#include <io.h>
#include <shared.h>
#include <buttons.h>

static inline float countsToMMHg(uint16_t c) { return 0.4766825f * c - 204.409f; }

// Modes
enum { MODE_FWD = 0, MODE_REV = 1, MODE_BEAT = 2 };
static inline int nextMode(int m){ return (m==MODE_FWD)?MODE_REV:(m==MODE_REV)?MODE_BEAT:MODE_FWD; }
static inline const char* modeName(int m){
  switch(m){ case MODE_FWD: return "FWD"; case MODE_REV: return "REV"; case MODE_BEAT: return "BEAT"; default: return "?"; }
}

// Phases for state machine
enum Phase { PH_RAMP_UP, PH_HOLD, PH_RAMP_DOWN, PH_DEAD };

// Map power% (10..100) to PWM command [PWM_FLOOR..PWM_MAX]
static inline int pwmFromPowerPct(int pct){
  if (pct < 10) pct = 10; if (pct > 100) pct = 100;
  float x = pct * (1.0f/100.0f);
  int pwm = (int)roundf(PWM_FLOOR + x * (PWM_MAX - PWM_FLOOR));
  if (pwm < PWM_FLOOR) pwm = PWM_FLOOR;
  if (pwm > PWM_MAX)   pwm = PWM_MAX;
  return pwm;
}

// Fallback: apply a command immediately if queue isn’t usable
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
    case CMD_SET_MODE: {
      int m = cmd.iarg; if (m < MODE_FWD || m > MODE_BEAT) m = MODE_FWD;
      G.mode.store(m, std::memory_order_relaxed);
      Serial.printf("[CTRL] (inline) SET_MODE -> %s(%d)\n", modeName(m), m);
    } break;
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

  // --- Button timing / combo ---
  uint32_t bPressMs = 0; bool bLongFired = false; const uint32_t LONG_MS = 500;
  bool comboActive = false, comboFired = false;
  uint32_t comboStartMs = 0, lastAPressMs = 0, lastBPressMs = 0;
  const uint32_t COMBO_WIN_MS = 120;

  // --- Control state machine vars ---
  Phase phase = PH_RAMP_UP;
  bool dirForward = true;             // current valve direction
  bool wantForward = true;            // desired direction from mode
  uint32_t phaseStartMs = millis();
  float pwmCmd = 0.0f;                // commanded PWM (float for slopes)
  int   targetPWM = pwmFromPowerPct(G.powerPct.load());
  float beat_hold_ms = 0.0f;          // dwell in HOLD per half-cycle
  int   lastModeSeen = G.mode.load();
  int   lastPowerSeen = G.powerPct.load();
  float lastBpmSeen = G.bpm.load();

  auto recomputeBeatBudget = [&](){
    float bpm = G.bpm.load();
    if (bpm < 0.5f) bpm = 0.5f; if (bpm > 60.0f) bpm = 60.0f;
    float Th_ms = (60.0f / bpm) * 1000.0f * 0.5f;  // half-cycle ms
    float tUp_ms   = (targetPWM - PWM_FLOOR) / PWM_SLOPE_UP   * 1000.0f;
    float tDown_ms = (targetPWM - PWM_FLOOR) / PWM_SLOPE_DOWN * 1000.0f;
    float tDead    = (float)DEADTIME_MS;
    float tHold = Th_ms - (tUp_ms + tDown_ms + tDead);
    if (tHold < 0) tHold = 0;
    beat_hold_ms = tHold;
  };

  Serial.printf("[CTRL] running on core %d\n", xPortGetCoreID());

  for (;;) {
    // --- loop timing ---
    uint32_t nowUs = micros();
    float dtMs = (nowUs - lastLoopStartUs) * 0.001f;
    lastLoopStartUs = nowUs;
    loopMsEMA = loopMsEMA * 0.9f + dtMs * 0.1f;
    G.loopMs.store(loopMsEMA, std::memory_order_relaxed);

    // --- debounced buttons ---
    BtnState ev{}; buttons_read_debounced(ev);
    const uint32_t nowMs = millis();

    if (ev.aPressEdge) lastAPressMs = nowMs;
    if (ev.bPressEdge) lastBPressMs = nowMs;

    // Combo detect
    if (!comboActive) {
      if ((ev.aPressed && (nowMs - lastBPressMs) <= COMBO_WIN_MS) ||
          (ev.bPressed && (nowMs - lastAPressMs) <= COMBO_WIN_MS) ||
          (ev.aPressed && ev.bPressed)) {
        comboActive = true; comboFired = false; comboStartMs = nowMs;
        bLongFired = false; // cancel B long
      }
    }

    if (comboActive) {
      if (!ev.aPressed && !ev.bPressed && !comboFired) {
        int cur = G.mode.load(std::memory_order_relaxed);
        int nm  = nextMode(cur);
        Cmd c{ CMD_SET_MODE, nm, 0.0f };
        if (!shared_cmd_post(c)) applyCmdInline(c);
        comboFired  = true;
        comboActive = false;
      }
    } else {
      // Singles
      if (ev.aPressEdge) {
        Cmd c{ CMD_TOGGLE_PAUSE, 0, 0.0f };
        if (!shared_cmd_post(c)) applyCmdInline(c);
      }

      if (ev.bPressEdge) { bPressMs = nowMs; bLongFired = false; }
      if (ev.bPressed && !bLongFired) {
        if (nowMs - bPressMs >= LONG_MS) {
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
    }

    // --- consume commands (non-blocking) ---
    Cmd cmd;
    bool modeChanged = false, powerChanged = false, bpmChanged = false;
    while (shared_cmdq() && xQueueReceive(shared_cmdq(), &cmd, 0) == pdTRUE) {
      switch (cmd.type) {
        case CMD_TOGGLE_PAUSE: {
          int p = G.paused.load(std::memory_order_relaxed);
          p = p ? 0 : 1; G.paused.store(p, std::memory_order_relaxed);
          Serial.printf("[CTRL] cmd: TOGGLE_PAUSE -> paused=%d\n", p);
        } break;
        case CMD_SET_POWER_PCT: {
          int pct = cmd.iarg; if (pct < 10) pct = 10; if (pct > 100) pct = 100;
          G.powerPct.store(pct, std::memory_order_relaxed);
          Serial.printf("[CTRL] cmd: SET_POWER_PCT -> %d%%\n", pct);
          powerChanged = true;
        } break;
        case CMD_SET_MODE: {
          int m = cmd.iarg; if (m < MODE_FWD || m > MODE_BEAT) m = MODE_FWD;
          G.mode.store(m, std::memory_order_relaxed);
          Serial.printf("[CTRL] cmd: SET_MODE -> %s(%d)\n", modeName(m), m);
          modeChanged = true;
        } break;
        case CMD_SET_BPM: {
          float bpm = cmd.farg; if (bpm < 0.5f) bpm = 0.5f; if (bpm > 60.0f) bpm = 60.0f;
          G.bpm.store(bpm, std::memory_order_relaxed);
          Serial.printf("[CTRL] cmd: SET_BPM -> %.1f\n", bpm);
          bpmChanged = true;
        } break;
      }
    }

    // --- update control phase machine (dry-run unless ENABLE_OUTPUTS=1) ---
    const bool paused = G.paused.load() != 0;
    const int  mode   = G.mode.load();
    const int  power  = G.powerPct.load();
    const float bpm   = G.bpm.load();

    // desired direction from mode (BEAT toggles later)
    wantForward = (mode != MODE_REV);

    // recompute target PWM if power changed
    if (powerChanged || power != lastPowerSeen) {
      targetPWM = paused ? 0 : pwmFromPowerPct(power);
      if (mode == MODE_BEAT) recomputeBeatBudget();
      lastPowerSeen = power;
    }

    if (modeChanged || mode != lastModeSeen) {
      // entering a new mode → interlock sequence: ramp down → deadtime → set direction → ramp up
      lastModeSeen = mode;
      if (mode == MODE_BEAT) { // reset for beat
        dirForward = true;
        phase = PH_RAMP_UP;
        phaseStartMs = nowMs;
        targetPWM = paused ? 0 : pwmFromPowerPct(power);
        recomputeBeatBudget();
      } else { // FWD/REV: enforce desired direction
        // start ramp-down if direction needs changing
        phase = (wantForward == dirForward) ? PH_RAMP_UP : PH_RAMP_DOWN;
        phaseStartMs = nowMs;
      }
    }

    if (bpmChanged || bpm != lastBpmSeen) {
      if (mode == MODE_BEAT) recomputeBeatBudget();
      lastBpmSeen = bpm;
    }

    // paused → force PWM=0, valve off (safe)
    if (paused) {
      pwmCmd = 0.0f;
      G.pwm.store(0, std::memory_order_relaxed);
      G.valve.store(0, std::memory_order_relaxed);
    #if ENABLE_OUTPUTS
      io_pwm_write(0);
      io_valve_write(false);
    #endif
    } else {
      // update phase by mode
      if (mode == MODE_FWD || mode == MODE_REV) {
        // if direction differs, do ramp-down → dead → ramp-up
        if ((wantForward != dirForward) && (phase != PH_RAMP_DOWN && phase != PH_DEAD)) {
          phase = PH_RAMP_DOWN;
          phaseStartMs = nowMs;
        }

        switch (phase) {
          case PH_RAMP_UP: {
            if (pwmCmd < PWM_FLOOR) pwmCmd = PWM_FLOOR;
            pwmCmd += PWM_SLOPE_UP * (dtMs * 0.001f);
            if (pwmCmd >= targetPWM) { pwmCmd = (float)targetPWM; phase = PH_HOLD; phaseStartMs = nowMs; }
          } break;
          case PH_HOLD: {
            // if target changed materially, adjust
            if (pwmCmd < targetPWM - 0.5f) { phase = PH_RAMP_UP; }
            else if (pwmCmd > targetPWM + 0.5f) { phase = PH_RAMP_DOWN; }
          } break;
          case PH_RAMP_DOWN: {
            pwmCmd -= PWM_SLOPE_DOWN * (dtMs * 0.001f);
            if (pwmCmd <= PWM_FLOOR) {
              pwmCmd = 0.0f; phase = PH_DEAD; phaseStartMs = nowMs;
            }
          } break;
          case PH_DEAD: {
            if ((nowMs - phaseStartMs) >= DEADTIME_MS) {
              dirForward = wantForward;
              phase = PH_RAMP_UP; phaseStartMs = nowMs;
            }
          } break;
        }
      } else { // MODE_BEAT
        // trapezoid each half-cycle: up -> hold -> down -> dead -> flip dir
        switch (phase) {
          case PH_RAMP_UP: {
            // set valve to current direction
            // (we publish valve below every tick)
            if (pwmCmd < PWM_FLOOR) pwmCmd = PWM_FLOOR;
            pwmCmd += PWM_SLOPE_UP * (dtMs * 0.001f);
            if (pwmCmd >= targetPWM) { pwmCmd = (float)targetPWM; phase = PH_HOLD; phaseStartMs = nowMs; }
          } break;
          case PH_HOLD: {
            if ((nowMs - phaseStartMs) >= (uint32_t)beat_hold_ms) {
              phase = PH_RAMP_DOWN; phaseStartMs = nowMs;
            }
          } break;
          case PH_RAMP_DOWN: {
            pwmCmd -= PWM_SLOPE_DOWN * (dtMs * 0.001f);
            if (pwmCmd <= PWM_FLOOR) { pwmCmd = 0.0f; phase = PH_DEAD; phaseStartMs = nowMs; }
          } break;
          case PH_DEAD: {
            if ((nowMs - phaseStartMs) >= DEADTIME_MS) {
              dirForward = !dirForward;              // toggle on each half-cycle
              phase = PH_RAMP_UP; phaseStartMs = nowMs;
              // recompute budget in case power/BPM changed mid-cycle
              recomputeBeatBudget();
            }
          } break;
        }
      }

      // publish and (optionally) actuate
      int outPWM = (int)roundf(pwmCmd);
      if (outPWM < 0) outPWM = 0; if (outPWM > PWM_MAX) outPWM = PWM_MAX;
      G.pwm.store((unsigned)outPWM, std::memory_order_relaxed);
      G.valve.store(dirForward ? 1u : 0u, std::memory_order_relaxed);

    #if ENABLE_OUTPUTS
      io_valve_write(dirForward);
      io_pwm_write((uint8_t)outPWM);
    #endif
    }

    // --- sensors (unchanged, safe) ---
    uint16_t rVent = io_adc_read_vent();
    uint16_t rAtr  = io_adc_read_atr();
    float v_mm = countsToMMHg(rVent);
    float a_mm = countsToMMHg(rAtr);
    G.vent_mmHg.store(v_mm, std::memory_order_relaxed);
    G.atr_mmHg.store(a_mm,  std::memory_order_relaxed);
    G.flow_ml_min.store(0.0f, std::memory_order_relaxed);

    // --- once/sec status line ---
    if (millis() - lastPrintMs >= 1000) {
      lastPrintMs = millis();
      float hz = 1000.0f / (loopMsEMA > 0 ? loopMsEMA : 1);
      int m = G.mode.load();
      const char* ph = (phase==PH_RAMP_UP)?"UP":(phase==PH_HOLD)?"HOLD":(phase==PH_RAMP_DOWN)?"DOWN":"DEAD";
      Serial.printf("[CTRL] Hz=%.1f Ms=%.3f paused=%d mode=%s dir=%s phase=%s pwm=%3u tgt=%3d bpm=%.1f hold=%.0fms\n",
                    hz, loopMsEMA,
                    G.paused.load(), modeName(m),
                    dirForward?"FWD":"REV", ph,
                    G.pwm.load(), targetPWM, G.bpm.load(), beat_hold_ms);
    }

    // blink LED ~1 Hz
    if (millis() - lastBlinkMs >= 1000) {
      lastBlinkMs = millis(); led = !led; digitalWrite(PIN_LED, led ? HIGH : LOW);
    }

    vTaskDelayUntil(&wake, period);
  }
}

void control_start_task() {
  xTaskCreatePinnedToCore(taskControl, "control", 6144, nullptr, 4, nullptr, CONTROL_CORE);
}
