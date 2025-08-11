#include <Arduino.h>
#include "../../build_options.h"
#include "dac.h"
#include "ihandlers.h"
#include "timer.h"
#include "eeprom.h"
#include "hw.h"
#include "ui.h"
#include "calibration.h"
#include "cv.h"

void setup() {
    Serial.begin(SERIAL_SPEED);

    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(LED_BLUE_PIN, OUTPUT);
    pinMode(LED_RED_PIN, OUTPUT);
    pinMode(GATE_PIN, OUTPUT);

    ui_initialize();

    calibration_read();
    ihInitialiseTimer();
    ihInitialiseInterrupts();

    DEBUG_PRINTLN("Hello, Theremin world!");
}

void loop() {
    int32_t pitch_v = 0;    // averaged pitch counter value
    int32_t pitch_l = 0;    // stored pitch counter value
    
    int32_t vol_v = 0;      // averaged volume counter value
    int32_t vol_l = 0;      // stored volume counter value

    int32_t clampedVol;     // clamped volume amplitude
    int32_t clampedPitch;   // clamped pitch for phase accumulator

mloop: // Main loop avoiding the GCC "optimization"

    if (pitchValueAvailable) {
        // --- Smooth pitch value (simple IIR low-pass filter) ---
        pitch_v = pitch_l + ((pitch - pitch_l) >> 2); // (low-pass) exponential moving average (EMA) approximation 75% previous, 25% new
        pitch_l = pitch_v;
        /*
        ((int32_t)pitchPotValue << 1)   // for half sensitivity
        ((int32_t)pitchPotValue << 3)   // for double sensitivity
        ((int32_t)pitchPotValue - 512) << 2   // center the pot at zero offset
        */
        #if PITCH_FIELD_MODE == PITCH_FIELD_MODE_SYMMETRICAL
            int32_t virtualPitch = pitch_v + (((int32_t)pitchPotValue - 512) << 2);  // apply offset
            clampedPitch = abs(pitchCalibrationBase - virtualPitch);
        #else
            clampedPitch = (pitchCalibrationBase - pitch_v) + 2048 - (pitchPotValue << 2);
        #endif
        if (clampedPitch < 0) { 
            // Lower clamp
            clampedPitch = 0;
        } else if (clampedPitch > 16383) {
            // Upper clamp
            // 16383 = 2^14 - 1
            // This value is shifted right by registerValue, or the selected octave (or pitch register).
            // If registerValue == 0, no shift: vPointerIncrement = 16383 (maximum playback speed).
            // vPointerIncrement is directly used to advance the phase pointer per sample.
            // Larger values -> faster wavetable traversal -> higher pitch.
            // Using a limit of 16383 ensures that:
            // - The shifted result stays within a reasonable frequency range, avoiding unplayably high tones.
            // - avoid wraparound in the phase accumulator or numeric overflow in later math.
            // - It fits well with common 10.6 fixed-point formats.
            clampedPitch = 16383;
        }
        setWavetableSampleAdvance((uint16_t)clampedPitch >> registerValue);
        
        #if CV_OUTPUT_MODE != CV_OUTPUT_MODE_OFF
            if (clampedPitch != pitch_p) { 
                // output new pitch CV value only if pitch value changed (saves runtime resources)
                pitch_p = clampedPitch;
                #if CV_OUTPUT_MODE == CV_OUTPUT_MODE_LOG
                    log_freq = log2U16((uint16_t)clampedPitch);
                    if (log_freq >= 37104) {
                        // 37104 = log2U16(512) + 48*4096/819
                        pitchCV = (int16_t)((819 * (log_freq - 37104)) >> 12);
                        pitchCV >>= registerValue - 1;
                    } else {
                        pitchCV = 0;
                    }
                #elif CV_OUTPUT_MODE == CV_OUTPUT_MODE_LINEAR
                    // 819Hz/V for Korg & Yamaha
                    pitchCV = clampedPitch >> 2 >> (registerValue - 1);
                #endif
                pitchCVAvailable = true;
            }
        #endif
        pitchValueAvailable = false;  // consume the flag
    }

    if (volumeValueAvailable) {
        // Average and clamp volume values
        vol = max(vol, 5000);
        vol_v = vol_l + ((vol - vol_l) >> 2); // (low-pass) exponential moving average (EMA) approximation
        vol_l = vol_v;

        if (audio_is_enabled()) {
            vol_v = DAC_12BIT_MAX - (volCalibrationBase - vol_v) / 2 + (volumePotValue << 2) - 1024;
        } else {
            vol_v = 0;
        }

        // Limit and set volume value
        vol_v = min(vol_v, DAC_12BIT_MAX);
        vol_v = max(vol_v, 0);
        clampedVol = vol_v >> 4;
        // Give vScaledVolume a pseudo-exponential characteristic:
        vScaledVolume = clampedVol * (clampedVol + 2);

        // if enabled output CV Volume ONLY (GATE output is being used to be measured by the display board)
        #if CV_OUTPUT_MODE == CV_OUTPUT_MODE_LOG || CV_OUTPUT_MODE == CV_OUTPUT_MODE_LINEAR
            // Most synthesizers "exponentiate" the volume CV themselves, thus send the "raw" volume for CV:
            volCV = vol_v;
            volumeCVAvailable = true;
        #endif
        volumeValueAvailable = false;
    }
    
    ui_do_loop();

    goto mloop; // End of main loop
}
