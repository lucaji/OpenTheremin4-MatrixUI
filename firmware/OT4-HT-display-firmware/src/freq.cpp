/**
 * @file freq.cpp
 * @brief Robust frequency measurement on AVR (ATmega328P) using Timer1 Input Capture.
 *
 * - Timer1 @ F_CPU/8 => 2 MHz tick (0.5 µs).
 * - Uses 32-bit "extended capture" timestamps (overflow-safe).
 * - Valid band defaults to ~30 Hz … 10 kHz (tunable).
 * - Optional EMA smoothing (alpha = 1/4) with NaN-safe handling.
 * - Returns 0.f if no signal or out of range for a while.
 * 
 * (c) 2025 Luca Cipressi (lucaji.github.io)
 * 
 * GNU GPL v3 or later.
 * 
 * REVISION HISTORY
 *     AUG 2025     Luca Cipressi (lucaji.githu.io) First commit
 * 
 */

#include "freq.h"
#include "../../build_options.h"
#include <util/atomic.h>
#include <math.h>     // isfinite()

/*
 *     Timer1 runs at 2 MHz, giving 0.5 µs resolution.
 *     Frequency accuracy is best from 30 Hz to 10 kHz.
 *     ICR1 (Input Capture Register) is used to get precise rising edge timestamps.
 *     Overflow handling allows low frequencies down to ~1 Hz, though less precise.
 */
 
// Frequency input on D8 (ICP1 pin)


// ====================== User-tunable constants =================================

// Timer1 prescaler = 8 => 2 MHz on 16 MHz boards.
#define TIMER1_PRESCALER    8UL
#define TIMER1_CLK_HZ     (F_CPU / TIMER1_PRESCALER)    // 2,000,000 at 16 MHz

// Expected frequency band (used for sanity checks).
#define FREQ_MIN_HZ     30.0f
#define FREQ_MAX_HZ     10000.0f

// Derived tick bounds for the valid band.
#define TICKS_MIN     (uint32_t)( (float)TIMER1_CLK_HZ / FREQ_MAX_HZ )    // ~200 ticks @10 kHz
#define TICKS_MAX     (uint32_t)( (float)TIMER1_CLK_HZ / FREQ_MIN_HZ )    // ~66666 ticks @30 Hz

// Milliseconds without new captures before reporting NAN.
#define NO_SIGNAL_TIMEOUT_MS    120U

// EMA smoothing (alpha = 1/4, integer-friendly).
#define EMA_NUM     1
#define EMA_DEN     4

// Enable input-capture noise canceler (adds ~0.25 µs qualification).
#define USE_NOISE_CANCELER    1

// ====================== Module state ===========================================

// Extended timebase: overflow count + ICR1 latched at the ISR.
static volatile uint16_t s_ovf = 0;
static volatile uint32_t s_icr32 = 0;
static volatile bool     s_newCap = false;

// Last processed capture (extended) in the foreground.
static uint32_t s_prevIcr32 = 0;

// Smoothed frequency and raw (for debugging/telemetry).
static float    s_freq_ema = NAN;
static float    s_freq_raw = NAN;

// Last time we successfully updated a valid reading.
static uint32_t s_lastOkMs = 0;

// ====================== Public API =============================================

/**
 * @brief Initialize Timer1 for input capture on D8 (ICP1).
 */
void freq_init() {
    pinMode(8, INPUT);    // D8 = ICP1 (PB0). Keep as high-Z input.

    // Stop Timer1 while configuring.
    TCCR1A = 0;
    TCCR1B = 0;
    TCCR1C = 0;
    TCNT1    = 0;

    // Clear any pending flags.
    TIFR1 = _BV(ICF1) | _BV(TOV1);

    // Prescaler = 8, capture on rising edge, optional noise canceler.
    TCCR1B =
#if USE_NOISE_CANCELER
    _BV(ICNC1) |    // Input Capture Noise Canceler
#endif
    _BV(ICES1)    |     // Capture on rising edge
    _BV(CS11);    // clk/8

    // Enable interrupts: Input Capture + Overflow.
    TIMSK1 = _BV(ICIE1) | _BV(TOIE1);

    s_ovf = 0;
    s_icr32 = 0;
    s_newCap = false;
    s_prevIcr32 = 0;
    s_freq_ema = NAN;
    s_freq_raw = NAN;
    s_lastOkMs = millis();

    DEBUG_PRINTLN(F("Frequency measurement started."));
}

/**
 * @brief Read current frequency in Hz (smoothed). Returns NAN if signal missing/out of range.
 * @return float Frequency in Hz (EMA), or NAN when no valid reading recently.
 */
float freq_read() {
    uint32_t cap = 0;
    bool have = false;

    // Atomically grab the latest capture (if any).
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    if (s_newCap) {
    cap = s_icr32;
    s_newCap = false;
    have = true;
    }
    }

    if (have) {
        if (s_prevIcr32 == 0) {
            // First capture seen: just prime the baseline.
            s_prevIcr32 = cap;
            // Keep existing EMA (likely NAN) until we have a full period.
            return s_freq_ema;
        }

        // Compute ticks between captures (handles wrap naturally with 32-bit).
        const uint32_t ticks = cap - s_prevIcr32;
        s_prevIcr32 = cap;

        // Sanity checks to reject spurious captures.
        if (ticks >= TICKS_MIN && ticks <= TICKS_MAX) {
            const float f = (float)TIMER1_CLK_HZ / (float)ticks;

            if (isfinite(f)) {
                s_freq_raw = f;

                // NaN-safe EMA: if EMA isn’t initialized, start from the first valid.
                if (!isfinite(s_freq_ema)) {
                    s_freq_ema = f;
                } else {
                    // ema += alpha*(f - ema); alpha = 1/4 => ema += (f-ema)/4
                    s_freq_ema += (f - s_freq_ema) * ((float)EMA_NUM / (float)EMA_DEN);
                }

                s_lastOkMs = millis();
            }
        }
        // else: ignore out-of-band ticks (glitches / runt pulses / stalls)
    }

    // Timeout: if we haven’t seen a valid update recently, expose NAN to the caller.
    if ((uint32_t)(millis() - s_lastOkMs) > NO_SIGNAL_TIMEOUT_MS) {
        return 0.f;
    }

    return s_freq_ema;
}

// ====================== ISRs ====================================================

/**
 * @brief Timer1 Input Capture ISR.
 * Builds a 32-bit "extended capture" timestamp using the overflow counter.
 *
 * Race-proofing vs. overflows: if TOV1 is set at the moment we read ICR1 and the
 * captured value is in the lower half, the edge likely happened after the overflow.
 * In that case, we attribute the capture to (ovf + 1).
 */
ISR(TIMER1_CAPT_vect) {
    const uint16_t icr = ICR1;     // Latched at the edge
    uint16_t ovf = s_ovf;

    // If an overflow occurred but hasn't been serviced yet AND the captured timer
    // value is in the "low" region, assign the capture to the post-overflow epoch.
    if ( (TIFR1 & _BV(TOV1)) && (icr < 0x8000) ) { ovf++; }
    s_icr32 = ( (uint32_t)ovf << 16 ) | (uint32_t)icr;
    s_newCap = true;
}

/**
 * @brief Timer1 Overflow ISR.
 * Extends the 16-bit timer into a 32-bit timebase by counting high words.
 */
ISR(TIMER1_OVF_vect) {
    s_ovf++;
}