#include "cv.h"


#if CV_OUTPUT_MODE == CV_OUTPUT_MODE_LOG
// calculate log2 of an unsigned from 1 to 65535 into a 4.12 fixed point unsigned
// To avoid use of log (double) function
// Fixed-point log2 approximation: input is uint16_t linear value
// Output is 4.12 unsigned fixed-point result (suitable for amplitude scaling, etc.)


#define LOG_SCALE    12
#define POLY_SHIFT   15
#define OUTPUT_SHIFT 3
#define BASE_1_0     32768          // 1.0 in s15 fixed-point

// Polynomial coefficients for log2(x) where x ∈ [1.0, 2.0]
#define POLY_A0  37
#define POLY_A1  46390
#define POLY_A2 -18778
#define POLY_A3   5155

uint16_t log2U16(uint16_t lin_input) {
    if (lin_input == 0)
        return 0;

    // Convert input to 16.16 fixed-point
    uint32_t long_lin = ((uint32_t)lin_input) << 16;
    uint32_t log_output = 0;

    // Fast bit-shifting to isolate integer part of log2
    if (long_lin >= (256UL << 16)) {  // 2^8
        log_output += (8 << LOG_SCALE);
        long_lin >>= 8;
    }
    if (long_lin >= (16UL << 16)) {   // 2^4
        log_output += (4 << LOG_SCALE);
        long_lin >>= 4;
    }
    if (long_lin >= (4UL << 16)) {    // 2^2
        log_output += (2 << LOG_SCALE);
        long_lin >>= 2;
    }
    if (long_lin >= (2UL << 16)) {    // 2^1
        log_output += (1 << LOG_SCALE);
        long_lin >>= 1;
    }

    // Now long_lin ∈ [1.0, 2.0) in 16.16 -> reduce to 17.15 for signed math
    long_lin >>= 1;

    int32_t x = (int32_t)long_lin - BASE_1_0; // x = (x - 1) in s15
    int32_t x2 = ((int64_t)x * x) >> POLY_SHIFT;
    int32_t x3 = ((int64_t)x2 * x) >> POLY_SHIFT;

    // Polynomial approximation: log2(x) ≈ A0 + A1·x + A2·x^2 + A3·x^3
    int32_t poly = POLY_A0
                 + (((int64_t)POLY_A1 * x) >> POLY_SHIFT)
                 + (((int64_t)POLY_A2 * x2) >> POLY_SHIFT)
                 + (((int64_t)POLY_A3 * x3) >> POLY_SHIFT);

    log_output += (poly >> OUTPUT_SHIFT);  // Adjust to 4.12 fixed-point

    return (uint16_t)log_output;
}
#endif
