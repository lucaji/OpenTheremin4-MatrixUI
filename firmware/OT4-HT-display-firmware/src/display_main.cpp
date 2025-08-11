/**
 * @file display_main.cpp
 * @brief UI state machine for the display board connected to OpenTheremin over UART.
 *
 * Responsibilities:
 *  - Render tuner views (numeric / bar / keyboard).
 *  - Handle single-button UX via short/long presses forwarded from the Theremin board.
 *  - Drive a small configuration menu (calibration, view mode, Concert A reference).
 *  - Show transient feedback for Register (octave) and Timbre (wavetable) changes.
 */
#include "display_main.h"
#include "../../build_options.h"
#include "HT1635.h"
#include "freq.h"
#include "../../eeprom.h"


// ==== Configuration & constants =================================================

/** EEPROM addresses (byte indices). */
static constexpr uint16_t EEPROM_TUNER_VIEW_MODE_ADDRESS = 0x00;
static constexpr uint16_t EEPROM_CONCERT_REF_A_ADDRESS   = 0x01;

/** UI timing (ms). */
static constexpr uint32_t UI_UPDATE_DELAY_MS = 100;
static constexpr uint32_t UI_TEMPORARY_PARAMETER_DISPLAY_MS = 1800;

/** Concert A bounds (Hz) for validation. */
static constexpr float CONCERT_A_MIN = 300.0f;
static constexpr float CONCERT_A_MAX = 600.0f;
static constexpr float CONCERT_A_DEFAULT = 440.0f;

/** Minimum frequency to consider “valid” for note rendering. */
static constexpr float MIN_VALID_FREQ = 10.0f;

// ==== Types =====================================================================

/**
 * @brief UI "page" / status machine for the display.
 */
typedef enum : uint8_t {
    tuner_view = 0,                 /**< Main tuner page. */

    // Menu items (cycled by short presses, confirmed by long press)
    menu_item_cal_enter,
    menu_item_pitch_display_mode_numeric,
    menu_item_pitch_display_mode_bar,
    menu_item_pitch_display_mode_keyboard,
    menu_item_concert_a_440,
    menu_item_concert_a_445,
    menu_item_concert_a_430,
    menu_item_concert_a_432,

    // Temporary feedback display (auto-exit)
    parameter_change_view_temporary_enter,
    parameter_change_view_temporary_wait,

} theremin_status_display_t;

/**
 * @brief What kind of temporary parameter feedback is being shown.
 */
typedef enum : uint8_t {
    none = 0,
    octave,     /**< Register change feedback. */
    timbre,     /**< Waveform change feedback. */
    status_msg, /**< Short textual status like PLAY!/MUTED/etc. */
    menu_txt    /**< Menu text; persists until user action. */
} display_status_parameter_t;

// ==== Globals (module-local) =====================================================

static HT1635 ht_display;

static theremin_state_t _theremin_state = muted;
static theremin_status_display_t _display_status = tuner_view;
static display_status_parameter_t _parameter_display_status = none;

static uint8_t _parameter_value = 0;
static float _concert_reference_a = CONCERT_A_DEFAULT;

// ==== Accessors =================================================================

theremin_state_t get_theremin_state() { return _theremin_state; }


/** Clear screen and return to the main tuner view. */
void restore_tuner_view() {
    _display_status = tuner_view;
    ht_display.update_display();
}


void display_ui_init() {
    ht_display.display_startup_logo();
    ht_display.update_display();
}

/**
 * @brief Show a short message and (optionally) mark it as a temporary parameter display.
 * @param kind One of @ref display_status_parameter_t.
 * @param txt  Null-terminated 5-char string for the 5-char area.
 */
static void display_status_msg(display_status_parameter_t kind, const char* txt) {
    _parameter_display_status = kind;
    // For menu text we keep the current page; for others we enter temporary view.
    if (kind != menu_txt) {
        _display_status = parameter_change_view_temporary_enter;
    }
    ht_display.print_string5(txt);
}

/** Draw menu page by enum. */
void display_menu(theremin_status_display_t status) {
    _display_status = status;
    HT1635::tuner_view_mode_t tuner_view_mode = ht_display.get_tuner_view_mode();
    switch (status) {
        case menu_item_cal_enter: display_status_msg(menu_txt, "CAL? "); break;
        
        case menu_item_pitch_display_mode_numeric:
            if (tuner_view_mode == HT1635::tuner_view_mode_t::numeric) {
                display_status_msg(menu_txt, "NUM <");
            } else {
                display_status_msg(menu_txt, "NUM? ");
            }
            break;
        case menu_item_pitch_display_mode_bar:
            if (tuner_view_mode == HT1635::tuner_view_mode_t::bar_graph) {
                display_status_msg(menu_txt, "BAR <");
            } else {
                display_status_msg(menu_txt, "BAR? ");
            }
            break;
        case menu_item_pitch_display_mode_keyboard:
            if (tuner_view_mode == HT1635::tuner_view_mode_t::piano_view) {
                display_status_msg(menu_txt, "PNO <");
            } else {
                display_status_msg(menu_txt, "PNO? ");
            }
            break;
        
        case menu_item_concert_a_440:
            if (fabsf(_concert_reference_a - 440.0f) < 0.01f) {
                display_status_msg(menu_txt, "A440<");
            } else {
                display_status_msg(menu_txt, "A440?");
            }
            break;
        case menu_item_concert_a_445:
            if (fabsf(_concert_reference_a - 445.0f) < 0.01f) {
                display_status_msg(menu_txt, "A445<");
            } else {
                display_status_msg(menu_txt, "A445?");
            }
            break;
        case menu_item_concert_a_430:
            if (fabsf(_concert_reference_a - 430.0f) < 0.01f) {
                display_status_msg(menu_txt, "A430<");
            } else {
                display_status_msg(menu_txt, "A430?");
            }
            break;
        case menu_item_concert_a_432:
            if (fabsf(_concert_reference_a - 432.0f) < 0.01f) {
                display_status_msg(menu_txt, "A432<");
            } else {
                display_status_msg(menu_txt, "A432?");
            }
            break;

        default: 
        break;
    }
}

/**
 * @brief Handle a user action forwarded from the Theremin (short/long press).
 * Short press: step or toggle; Long press: confirm/apply for the current menu item.
 * @param shortPress true for short press, false for long press.
 */
void handle_user_action(bool shortPress) {
    HT1635::tuner_view_mode_t tuner_view_mode = ht_display.get_tuner_view_mode();
    bool shall_restore_tuner_view = false;
    switch (_display_status) {
        case tuner_view:
            if (shortPress) {
                if (_theremin_state == muted) {
                    Serial.write(STATE_CMD_UNMUTE);
                } else if (_theremin_state == playing) {
                    Serial.write(STATE_CMD_MUTE);
                }
            } else {
                display_menu(menu_item_cal_enter);
            }
        break;

        case menu_item_cal_enter:
            if (shortPress) {
                display_menu(menu_item_pitch_display_mode_numeric);
            } else {
                Serial.write(STATE_CMD_CALIBRATION);
            }
            break;

        case menu_item_pitch_display_mode_numeric:
            if (shortPress) {
                display_menu(menu_item_pitch_display_mode_bar);
            } else {
                tuner_view_mode = ht_display.set_tuner_view_mode(HT1635::tuner_view_mode_t::numeric);
                EEPROM.put(EEPROM_TUNER_VIEW_MODE_ADDRESS, tuner_view_mode);
                shall_restore_tuner_view = true;
            }
            break;

        case menu_item_pitch_display_mode_bar:
            if (shortPress) {
                display_menu(menu_item_pitch_display_mode_keyboard);
            } else {
                tuner_view_mode = ht_display.set_tuner_view_mode(HT1635::tuner_view_mode_t::bar_graph);
                EEPROM.put(EEPROM_TUNER_VIEW_MODE_ADDRESS, tuner_view_mode);
                shall_restore_tuner_view = true;
            }
            break;

        case menu_item_pitch_display_mode_keyboard:
            if (shortPress) {
                display_menu(menu_item_concert_a_440);
            } else {
                tuner_view_mode = ht_display.set_tuner_view_mode(HT1635::tuner_view_mode_t::piano_view);
                EEPROM.put(EEPROM_TUNER_VIEW_MODE_ADDRESS, tuner_view_mode);
                shall_restore_tuner_view = true;
            }
            break;

        case menu_item_concert_a_440:
            if (shortPress) {
                display_menu(menu_item_concert_a_445);
            } else {
                _concert_reference_a = 440.0f;
                EEPROM.put(EEPROM_CONCERT_REF_A_ADDRESS, _concert_reference_a); 
                shall_restore_tuner_view = true;
            }
            break;

        case menu_item_concert_a_445:
            if (shortPress) {
                display_menu(menu_item_concert_a_430);
            } else {
                _concert_reference_a = 445.0f;
                EEPROM.put(EEPROM_CONCERT_REF_A_ADDRESS, _concert_reference_a); 
                shall_restore_tuner_view = true;
            }
            break;

        case menu_item_concert_a_430:
            if (shortPress) {
                display_menu(menu_item_concert_a_432);
            } else {
                _concert_reference_a = 430.0f;
                EEPROM.put(EEPROM_CONCERT_REF_A_ADDRESS, _concert_reference_a); 
                shall_restore_tuner_view = true;
            }
            break;

        case menu_item_concert_a_432:
            if (shortPress) {
                shall_restore_tuner_view = true;
            } else {
                _concert_reference_a = 432.0f;
                EEPROM.put(EEPROM_CONCERT_REF_A_ADDRESS, _concert_reference_a); 
                shall_restore_tuner_view = true;
            }
            break;

        default: break;
    }
    if (shall_restore_tuner_view) {
        restore_tuner_view();
    }
}


/**
 * @brief Read persistent settings from EEPROM and validate.
 */
static void settings_read() {
    // View mode
    int8_t tvm = 0;
    EEPROM.get(EEPROM_TUNER_VIEW_MODE_ADDRESS, tvm);
    if (tvm < 0 || tvm >= HT1635::tuner_view_mode_t::tuner_view_mode_t_last) {
        tvm = HT1635::tuner_view_mode_t::piano_view;
    }
    HT1635::tuner_view_mode_t tuner_view_mode = static_cast<HT1635::tuner_view_mode_t>(tvm);
    ht_display.set_tuner_view_mode(tuner_view_mode);

    float ref_a = 0.0f;
    EEPROM.get(EEPROM_CONCERT_REF_A_ADDRESS, ref_a);
    if (ref_a < CONCERT_A_MIN || ref_a > CONCERT_A_MAX) {
        ref_a = CONCERT_A_DEFAULT;
    }
    _concert_reference_a = ref_a;
}

/**
 * @brief Initialize the HT1635 display and show startup screen.
 */
void setup() {
    Serial.begin(SERIAL_SPEED);
    settings_read();
    ht_display.begin();
    display_ui_init();
    freq_init();
    DEBUG_PRINTLN(F("Display UI ready."));
    _display_status = tuner_view;
}

// ==== Arduino lifecycle ==========================================================

void loop() {
    static unsigned long tuner_view_update_old_tick = 0;
    static unsigned long parameter_view_update_old_tick = 0;

    float raw = freq_read();
    unsigned long t = millis();
    
    // ---- UART protocol from Theremin ------------------------------------------
    if (Serial.available()) {
        uint8_t b = Serial.read();
        switch (b) {
            case STATE_CMD_MUTE:
                _theremin_state = muted;
                display_status_msg(status_msg, "MUTED");
                break;

            case STATE_CMD_UNMUTE:
                _theremin_state = playing;
                display_status_msg(status_msg, "PLAY!");
                break;

            case STATE_CMD_CALIBRATION:
                _theremin_state = calibrating;
                display_status_msg(status_msg, "-CAL-");
                ht_display.set_blink_mode(HT1635::blink_setting_t::BLINK_1Hz);
                break;

            case STATE_CMD_CALIBRATION_SUCCESS:
                _theremin_state = playing;
                ht_display.set_blink_mode(HT1635::blink_setting_t::BLINK_OFF);
                display_status_msg(status_msg, "CALOK");
                break;

            case STATE_CMD_CALIBRATION_ERROR:
                _theremin_state = muted;
                ht_display.set_blink_mode(HT1635::blink_setting_t::BLINK_OFF);
                display_status_msg(status_msg, "CALER");
                _display_status = menu_item_cal_enter;
                break;

            case STATE_CMD_BUTTON_SHORT_PRESS:
                handle_user_action(true);
                break;

            case STATE_CMD_BUTTON_LONG_PRESS:
                handle_user_action(false);
                break;

            default:
                // Parameter ranges: Register (octave) and Timbre (wavetable index).
                if (b >= STATE_CMD_REGISTER_LOW && b <= STATE_CMD_REGISTER_HIGH) {
                    _display_status = parameter_change_view_temporary_enter;
                    _parameter_display_status = octave;
                    _parameter_value = b;
                } else if (b >= STATE_CMD_WAVEFORM_BASE && b <= (STATE_CMD_WAVEFORM_BASE + 0x9)) {
                    _display_status = parameter_change_view_temporary_enter;
                    _parameter_display_status = timbre;
                    _parameter_value = b - STATE_CMD_WAVEFORM_BASE;
                } else {
                    // Unrecognized; ignore or DEBUG_PRINT(b);
                    //DEBUG_PRINT(b); // echo
                }
                break;
        }
    }

    switch (_display_status) {
        case tuner_view:
            if (t - tuner_view_update_old_tick > UI_UPDATE_DELAY_MS) {
                tuner_view_update_old_tick = t;
                ht_display.render_pitch_and_drift(raw, _concert_reference_a, MIN_VALID_FREQ);
            }
            break;

        case parameter_change_view_temporary_enter:
            parameter_view_update_old_tick = t;
            switch (_parameter_display_status) {
                case octave: {
                    char txt[6] = "OCT+1";
                    if (_parameter_value == STATE_CMD_REGISTER_LOW) {
                        txt[3] = '-';
                    } else if (_parameter_value == STATE_CMD_REGISTER_MID) {
                        txt[4] = '0';
                    } 
                    ht_display.print_string5(txt);
                } break;

                case timbre: {
                    char txt[6] = "WAV  ";
                    txt[4] = char((_parameter_value % 10) + '0');
                    ht_display.print_string5(txt);
                } break;

                default: break;
            }
            _display_status = parameter_change_view_temporary_wait;
            break;

        case parameter_change_view_temporary_wait:
            if (_parameter_display_status == menu_txt || _theremin_state == calibrating) {
                // menu items stays until user exits menu.
                // calibration stays until success or failure.
            } else if (t - parameter_view_update_old_tick > UI_TEMPORARY_PARAMETER_DISPLAY_MS) {
                _display_status = tuner_view;
                _parameter_display_status = none;
                restore_tuner_view();
            }

            break;

        default: break;
    }
        

        


}
