#include <Arduino.h>

#ifndef _DISPLAY_MAIN_H_
#define _DISPLAY_MAIN_H_

#include "HT1635.h"

/**
 * @brief High-level Theremin state echoed by the instrument.
 */
typedef enum : uint8_t {
    muted = 0,      /**< Audio output muted. */
    playing,        /**< Normal performance. */
    calibrating     /**< In calibration routine. */
} theremin_state_t;

/** @return The current Theremin state. */
theremin_state_t get_theremin_state();


#endif // _DISPLAY_MAIN_H_