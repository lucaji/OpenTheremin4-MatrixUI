#include "ui.h"
#include "ihandlers.h"
#include "timer.h"
#include "hw.h"
#include "../../build_options.h"
#include "calibration.h"

#define UI_BUTTON_LONG_PRESS_DURATION   60000


typedef enum {
    theremin_state_t_muted,
    theremin_state_t_playing,
    theremin_state_t_calibrating,
} theremin_state_t;
theremin_state_t _theremin_state = theremin_state_t_muted;

bool audio_is_enabled() {
    return _theremin_state == theremin_state_t_playing;
}

typedef enum {
    button_state_t_released,
    button_state_t_long_press_wait,
} button_state_t;
button_state_t _button_state = button_state_t_released;

// Potentiometer variables, hysteresis and scaling
#define HYST_SCALE 0.95
static const int16_t pot_register_selection_hysteresis = 1024.0 / 3 * HYST_SCALE;   // only three position octave selection
static const int16_t pot_waveform_selection_hysteresis = 1024.0 / num_wavetables * HYST_SCALE;  // map the waveform selection poti depending on how many waveforms are being loaded in the DDS generator
static const int16_t pot_rf_virtual_field_adjust_hysteresis = 1024 / 64;    // reduce the volume and pitch field potis to 64 steps to slightly reduce audio jitter
int16_t pitchPotValue = 0, pitchPotValueL = 0;
int16_t volumePotValue = 0, volumePotValueL = 0;
int16_t registerPotValue = 0, registerPotValueL = 0;
int16_t wavePotValue = 0, wavePotValueL = 0;
uint8_t registerValue = 2; // octave register
uint8_t registerValueL = 2; // octave register old value

/**
 * @brief Read all the potentiometer position and update the changed values
 *        if the hysteresis constant have been surpassed.
 * 
 * @param force
 *        optional boolean flag to force the updates (at power-on)
 */
void ui_potis_read_all(bool force = false) {
    pitchPotValueL = analogRead(PITCH_POT);
    if (force || abs(pitchPotValue - pitchPotValueL) >= pot_rf_virtual_field_adjust_hysteresis) { 
        pitchPotValue = pitchPotValueL;
    }
    
    volumePotValueL = analogRead(VOLUME_POT);
    if (force || abs(volumePotValue - volumePotValueL) >= pot_rf_virtual_field_adjust_hysteresis) { 
        volumePotValue = volumePotValueL;
    }

    registerPotValueL = analogRead(REGISTER_SELECT_POT);
    if (force || abs(registerPotValue - registerPotValueL) >= pot_register_selection_hysteresis) { 
        registerPotValue = registerPotValueL;
        // register pot offset configuration:
        // Left = -1 octave, Center = 0, Right = +1 octave
        if (registerPotValue > pot_register_selection_hysteresis * 2) {
            registerValueL = 1;
            DEBUG_PRINTLN(F("OCT+1"));
        } else if (registerPotValue < pot_register_selection_hysteresis) {
            registerValueL = 3;
            DEBUG_PRINTLN(F("OCT-1"));
        } else {
            registerValueL = 2;
            DEBUG_PRINTLN(F("OCT+0"));
        }
        if (registerValueL != registerValue) {
        registerValue = registerValueL;
    }
    }

    wavePotValueL = analogRead(WAVE_SELECT_POT);
    if (force || abs(wavePotValue - wavePotValueL) >= pot_waveform_selection_hysteresis) {
        wavePotValue = wavePotValueL;
        // map 0–1023 to 0–(num_wavetables - 1)
        uint16_t scaled = ((uint32_t)wavePotValue * num_wavetables) / 1024;
        if (scaled >= num_wavetables) scaled = num_wavetables - 1;    // extra safety
        if (scaled != vWavetableSelector) {
            vWavetableSelector = scaled;
            DEBUG_PRINT(F("WAV="));DEBUG_PRINTLN(scaled);
        }
    }
}

void ui_initialize() {
    HW_LED_RED_ON; // muted state at power-cycle.
    ui_potis_read_all(true);
}

void ui_button_action() {
    switch (_button_state) {
        case button_state_t_released:
            if (HW_BUTTON_PRESSED) { 
                resetTimer();
                _button_state = button_state_t_long_press_wait;
            }
        break;

        case button_state_t_long_press_wait:
            if (HW_BUTTON_RELEASED) { 
                _button_state = button_state_t_released;
                Serial.write(STATE_CMD_BUTTON_SHORT_PRESS);
            } else if (timerExpired(UI_BUTTON_LONG_PRESS_DURATION)) {
                _button_state = button_state_t_released;
                Serial.write(STATE_CMD_BUTTON_LONG_PRESS);
                while (HW_BUTTON_PRESSED) {}
            }
        break;
    }
}

void ui_do_loop() {
    if (Serial.available()) {
        uint8_t b = Serial.read();
        switch (b) {
            case STATE_CMD_CALIBRATION: {
                Serial.write(STATE_CMD_CALIBRATION);
                HW_LED_BLUE_ON; HW_LED_RED_ON;
                
                #if AUDIO_FEEDBACK_MODE == AUDIO_FEEDBACK_ON
                    playTone(MIDDLE_C, 150, 25);
                    playTone(MIDDLE_C * 2, 150, 25);
                    playTone(MIDDLE_C * 4, 150, 25);
                #endif

                // signal the player to move the hands away from antennas.
                for (int i = 0; i<10; i++) {
                    millitimer(200 - (i * 10));
                    HW_LED_BLUE_TOGGLE; HW_LED_RED_TOGGLE;
                }
                // pink color for calibration
                HW_LED_BLUE_ON; HW_LED_RED_ON;
                _theremin_state = theremin_state_t_calibrating;

                bool success = calibration_start();
                if (success) {
                    HW_LED_BLUE_ON; HW_LED_RED_OFF;
                    _theremin_state = theremin_state_t_playing;

                    #if AUDIO_FEEDBACK_MODE == AUDIO_FEEDBACK_ON
                        playTone(MIDDLE_C * 2, 150, 25);
                        playTone(MIDDLE_C * 2, 150, 25);
                    #endif
                } else {
                    HW_LED_BLUE_OFF;
                    for (int i = 0; i<10; i++) {
                        millitimer(200 - (i * 10));
                        HW_LED_RED_TOGGLE;
                    }
                    #if AUDIO_FEEDBACK_MODE == AUDIO_FEEDBACK_ON
                        playTone(MIDDLE_C * 4, 150, 25);
                        playTone(MIDDLE_C, 150, 25);
                    #endif
                    HW_LED_RED_ON;
                }
            } break;

            case STATE_CMD_MUTE:
                HW_LED_BLUE_OFF; HW_LED_RED_ON;
                _theremin_state = theremin_state_t_muted;
                Serial.write(STATE_CMD_MUTE);
                break;

            case STATE_CMD_UNMUTE:
                HW_LED_BLUE_ON; HW_LED_RED_OFF;
                _theremin_state = theremin_state_t_playing;
                Serial.write(STATE_CMD_UNMUTE);
                break;

            default:
                //DEBUG_PRINT(b); // echo
                break;
        }
    }
    ui_button_action();
    ui_potis_read_all();
}


#if AUDIO_FEEDBACK_MODE == AUDIO_FEEDBACK_ON
    const float MIDDLE_C = 261.6;


void playTone(float hz, uint16_t milliseconds = 500, uint8_t volume = 255) {
    const float HZ_SCALING_FACTOR = 2.09785;
    bool was_audio_active = _audio_output_is_enabled;
    _audio_output_is_enabled = true; // force emit the tone
    setWavetableSampleAdvance((uint16_t)(hz * HZ_ADDVAL_FACTOR));
    millitimer(milliseconds);
    _audio_output_is_enabled = was_audio_active; // restore previous mute state
}

#endif