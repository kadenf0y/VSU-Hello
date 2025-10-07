#include <Arduino.h>
#include "app_config.h"
#include <io.h>

static const int LEDC_CH    = 0;
static const int LEDC_TIMER = 0;

void io_init() {
  // Valve pin (OFF)
  pinMode(PIN_VALVE, OUTPUT);
  digitalWrite(PIN_VALVE, LOW);

  // PWM: LEDC high-speed, 8-bit at ~20 kHz
  ledcSetup(LEDC_CH, 20000, 8);
  ledcAttachPin(PIN_PUMP_PWM, LEDC_CH);
  ledcWrite(LEDC_CH, 0);

  // ADC
  analogReadResolution(12);
  pinMode(PIN_PRESS_VENT, INPUT);
  pinMode(PIN_PRESS_ATR, INPUT);
  analogSetPinAttenuation(PIN_PRESS_VENT, ADC_6db);
  analogSetPinAttenuation(PIN_PRESS_ATR,  ADC_6db);
}

uint16_t io_adc_read_vent() { return analogRead(PIN_PRESS_VENT); }
uint16_t io_adc_read_atr()  { return analogRead(PIN_PRESS_ATR);  }
void     io_valve_write(bool on) { digitalWrite(PIN_VALVE, on ? HIGH : LOW); }
void     io_pwm_write(uint8_t pwm){ ledcWrite(LEDC_CH, pwm); }
