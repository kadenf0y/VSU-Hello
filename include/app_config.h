#pragma once

// ==== Pins (your hardware map) ====
#define PIN_BTN_A        14
#define PIN_BTN_B        13
#define PIN_VALVE        23
#define PIN_PUMP_PWM     19
#define PIN_PRESS_VENT   32   // ADC1
#define PIN_PRESS_ATR    33   // ADC1
#define PIN_FLOW         27   // (we’ll stub later)

// Optional onboard LED (many ESP32 dev boards use GPIO 2)
#define PIN_LED          2

// ==== PWM bounds (we’ll tune later) ====
#define PWM_MAX          255
#define PWM_FLOOR        165

// ==== Rates ====
#define CONTROL_HZ       500

// --- TEMP: use safer pins for dev board buttons (avoid JTAG pins 13/14) ---
#ifndef USE_TEST_BUTTON_PINS
#define USE_TEST_BUTTON_PINS 1
#endif
