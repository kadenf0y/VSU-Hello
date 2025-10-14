#pragma once
#include <Arduino.h>

/*
  ========= Hardware map (ESP32 Dev Module) ========

  Buttons are momentary to GND (active-LOW), with internal pull-ups.
  Pressure sensors on ADC1 so Wi-Fi later won’t block them.
*/

// ==== Calibration knobs (edit these numbers) ====
// y = gain * x + offset  (x = current 'raw' reading you're storing)
struct PressCal { float gain; float offset; };

static constexpr PressCal CAL_ATR  = { 1.000f, -700.0f };   // atrium scale & offset
static constexpr PressCal CAL_VENT = { 1.000f, -700.0f };   // ventricle scale & offset


#define PIN_BTN_A        14     // Pause (active-LOW)
#define PIN_BTN_B        13     // Power nudge (active-LOW)
#define PIN_VALVE        23     // Digital out → MOSFET driver (direction valve)
#define PIN_PUMP_PWM     19     // LEDC PWM out → pump MOSFET driver
#define PIN_PRESS_VENT   32     // ADC1
#define PIN_PRESS_ATR    33     // ADC1
#define PIN_FLOW         27     // Flow sensor (digital), stubbed now
#define PIN_LED           2     // Onboard LED on many dev boards

// ---- Inline helpers (visible to all .cpp that include this header) ----
static inline float scale_atrium(float raw)   { return raw * CAL_ATR.gain  + CAL_ATR.offset; }
static inline float scale_vent(float raw)     { return raw * CAL_VENT.gain + CAL_VENT.offset; }

// -------- FLOW SENSOR CONFIG --------
// Edge polarity: true = FALLING (idle HIGH, pulses LOW). false = RISING (idle LOW, pulses HIGH)
static constexpr bool FLOW_EDGE_FALLING = true;

// Dynamic sampling window (ms)
static constexpr uint16_t FLOW_WINDOW_MS_MIN = 50;     // fastest window
static constexpr uint16_t FLOW_WINDOW_MS_MAX = 500;    // slowest window
static constexpr float    FLOW_TARGET_PULSES = 30.0f;  // target pulses per window

// DIGITEN G3/8" typical: ~23.6 Hz per 1 L/min  →  Q[mL/min] = Hz * 1000 / 23.6
static constexpr float FLOW_HZ_PER_LPM       = 23.6f;
static constexpr float FLOW_HZ_TO_ML_MIN     = 1000.0f / FLOW_HZ_PER_LPM;

// helper
static inline float flow_hz_to_mlmin(float hz){ return hz * FLOW_HZ_TO_ML_MIN; }

/*
  ========= Rates / bounds =========
*/

#define CONTROL_HZ       500    // Control loop 500 Hz (2 ms period)

#define PWM_MAX          255    // 8-bit LEDC
#define PWM_FLOOR        165    // your stall floor + margin (~165)

#define PWM_SLOPE_UP     350.0f // PWM counts per second (up)
#define PWM_SLOPE_DOWN   350.0f // PWM counts per second (down)
#define DEADTIME_MS      100    // valve settle time

#ifndef STATUS_MS
#define STATUS_MS        200    // status prints every 200 ms (~5 Hz)
#endif

/*
  ========= Safety =========
  Start in DRY-RUN mode (don’t drive the real outputs).
  Set to 1 once wiring is verified and you’re ready to move actuators.
*/
#ifndef ENABLE_OUTPUTS
#define ENABLE_OUTPUTS   1
#endif

/*
  ========= Core selection =========
  ESP32 core mapping: 0 = PRO (Wi-Fi), 1 = APP (Arduino loop).
  Control task on Core 1; web (later) on Core 0.
*/
#define CORE_PRO         0
#define CORE_APP         1
#define CONTROL_CORE     CORE_APP
#define WEB_CORE         CORE_PRO
