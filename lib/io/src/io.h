#pragma once
#include <Arduino.h>

/* Hardware I/O wrappers (keep main logic clean) */

void     io_init();                  // set up pins, ADC, LEDC PWM

uint16_t io_adc_read_vent();         // raw 12-bit counts
uint16_t io_adc_read_atr();

void     io_valve_write(bool on);    // set valve direction line
void     io_pwm_write(uint8_t pwm);  // write 0..255 to LEDC
