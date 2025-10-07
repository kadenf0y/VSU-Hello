#pragma once
#include <Arduino.h>
#include "app_config.h"


// initialize pins and PWM hardware (safe defaults)
void io_init();

// helpers (safe no-ops / thin wrappers)
uint16_t io_adc_read_vent();
uint16_t io_adc_read_atr();
void     io_valve_write(bool on);
void     io_pwm_write(uint8_t pwm);
