#include "timer.h"

volatile uint16_t timer = 0;

void ticktimer (uint16_t ticks) {
  timer = 0;
  while (timerUnexpired(ticks));
}

void millitimer (uint16_t milliseconds) {
  ticktimer(millisToTicks(milliseconds));
}
