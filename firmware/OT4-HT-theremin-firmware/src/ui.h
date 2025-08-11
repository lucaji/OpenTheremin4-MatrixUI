#include <Arduino.h>

#ifndef _DISPLAY_INTERFACE_H_
#define _DISPLAY_INTERFACE_H_

extern int16_t pitchPotValue;
extern int16_t volumePotValue;
extern uint8_t registerValue;

void ui_initialize();
void ui_do_loop();

bool audio_is_enabled();

#endif // _DISPLAY_INTERFACE_H_