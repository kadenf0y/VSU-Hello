#pragma once
#include <Arduino.h>
#include "app_config.h"

void buttons_init();
void buttons_sample(bool &aPressed, bool &bPressed);
void buttons_debug_print();   // <-- new helper
