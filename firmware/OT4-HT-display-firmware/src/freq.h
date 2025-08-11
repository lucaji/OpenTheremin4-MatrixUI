/**
 * @file freq.h
 * @brief interface code to measure a frequency on an Arduino UNO
 * 
 * (c) 2025 Luca Cipressi (lucaji.github.io)
 * 
 * GNU GPL v3 or later.
 * 
 * REVISION HISTORY
 *         AUG 2025     Luca Cipressi (lucaji.githu.io) First commit
 * 
 */

#include <Arduino.h>

#ifndef _FREQUENCYMETER_H_
#define _FREQUENCYMETER_H_

void freq_init();
void freq_loop();

float freq_read();

#endif // _FREQUENCYMETER_H_