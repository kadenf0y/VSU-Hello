#include "io.h"
#include "app_config.h"
#include <esp32-hal-adc.h>  // analogSetPinAttenuation

// LEDC config: channel 0, timer 0, 20 kHz, 8-bit
static const int LEDC_CH    = 0;
static const int LEDC_TIMER = 0;

void io_init() {
  // Valve default OFF (LOW)
  pinMode(PIN_VALVE, OUTPUT);
  digitalWrite(PIN_VALVE, LOW);

  // PWM @ 20 kHz, 8-bit resolution
  ledcSetup(LEDC_CH, 20000, 8);
  ledcAttachPin(PIN_PUMP_PWM, LEDC_CH);
  ledcWrite(LEDC_CH, 0);

  // ADC1 inputs; sensors ~1.0–1.3 V => 0 dB attenuation is best
  analogReadResolution(12);
  pinMode(PIN_PRESS_VENT, INPUT);
  pinMode(PIN_PRESS_ATR, INPUT);
  analogSetPinAttenuation(PIN_PRESS_VENT, ADC_0db);
  analogSetPinAttenuation(PIN_PRESS_ATR,  ADC_0db);

  // Flow input (we’ll implement counting later)
  pinMode(PIN_FLOW, INPUT);
}

uint16_t io_adc_read_vent() { return analogRead(PIN_PRESS_VENT); }
uint16_t io_adc_read_atr()  { return analogRead(PIN_PRESS_ATR);  }

void io_valve_write(bool on) { digitalWrite(PIN_VALVE, on ? HIGH : LOW); }
void io_pwm_write(uint8_t pwm){ ledcWrite(LEDC_CH, pwm); }
