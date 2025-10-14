// Flow sensor frequency test (ESP32, GPIO 27)
// Wire sensor signal (yellow) to GPIO 27 (level-shifted to ~3.3V).
// Choose FALLING vs RISING below depending on your sensor’s idle level.

#include <Arduino.h>

const int PIN_FLOW = 27;
const bool USE_FALLING_EDGE = true;      // set false to use RISING instead
const uint32_t SAMPLE_MS = 500;          // print interval

volatile uint32_t g_pulses = 0;
volatile uint32_t g_lastMicros = 0;      // simple deglitch

void flow_isr() {
  uint32_t now = micros();
  // ignore glitches faster than 100 us (10 kHz limit)
  if (now - g_lastMicros >= 100) {
    g_pulses++;
    g_lastMicros = now;
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) { /* wait for USB on some boards */ }

  // Pin is biased by your hardware (divider/level shifter). No internal pull-up here.
  pinMode(PIN_FLOW, INPUT);

  attachInterrupt(
    digitalPinToInterrupt(PIN_FLOW),
    flow_isr,
    USE_FALLING_EDGE ? FALLING : RISING
  );

  Serial.println(F("Flow frequency test"));
  Serial.println(F("Columns: ms, pulses, Hz"));
}

void loop() {
  static uint32_t t0 = millis();
  uint32_t now = millis();
  if (now - t0 >= SAMPLE_MS) {
    uint32_t window_ms = now - t0;
    t0 = now;

    // snapshot & reset safely
    noInterrupts();
    uint32_t cnt = g_pulses;
    g_pulses = 0;
    interrupts();

    float hz = (window_ms > 0) ? (cnt * 1000.0f / window_ms) : 0.0f;

    Serial.print(now);
    Serial.print(", ");
    Serial.print(cnt);
    Serial.print(", ");
    Serial.println(hz, 2);
  }
}
