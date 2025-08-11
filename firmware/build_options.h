/**
 * @file ../../build_options.h
 * @brief Build options for OpenTheremin + matrix LED mod
 *
 * Define directives of custom build options for both the DISPLAY board and the
 * adapted ThereminV4 firmware that supports the UI functions.
 * 
 *
 * REVISION HISTORY
 * Augugust 2025 Luca Cipressi (lucaji.github.io) first commit.
 * (c) GNU GPL v3 or later.
 */

#ifndef _BUILD_COMMON_H_
#define _BUILD_COMMON_H_


#define PITCH_ALTERATION_MODE_SHARP     0x1
#define PITCH_ALTERATION_MODE_FLAT      0x2
#define PITCH_ALTERATION_MODE           PITCH_ALTERATION_MODE_SHARP

/*
 * SERIAL_SPEED constant
 * 
 * configures the serial port speed which is used to send
 * status information to the display board (calibration, muted, etc.)
 * 
 */
#define SERIAL_SPEED        38400 // serial com speed

/*
 * SERIAL_DEBUG_MESSAGES
 * 
 * emits debug information via serial port if defined.
 * to mute debug printouts, comment the below line out.
 * 
 */
#define SERIAL_DEBUG_MESSAGES
#ifdef SERIAL_DEBUG_MESSAGES
    #define DEBUG_PRINT(x)     Serial.print(x)
    #define DEBUG_PRINTLN(x)   Serial.println(x)
#else
    #define DEBUG_PRINT(x)     ((void)0)
    #define DEBUG_PRINTLN(x)   ((void)0)
#endif

/*
 * AUDIO_FEEDBACK_MODE
 * 
 * - AUDIO_FEEDBACK_ON
 *   enables short audio feedbacks (beeps) at startup and calibration
 *   along the visual indication of the current mode.
 * 
 * - AUDIO_FEEDBACK_OFF
 *   disables the audio feedback (silent), keeping the visual
 *   indication of the current mode.
 * 
 */
#define AUDIO_FEEDBACK_ON   0
#define AUDIO_FEEDBACK_OFF  1
#define AUDIO_FEEDBACK_MODE AUDIO_FEEDBACK_OFF

/*
 * CV_OUTPUT_MODE sets options for the CV output jack
 *
 * for better audio output quality, set it off as it saves precious resources
 * especially if you don't need CV/GATE outputs. Refer to each option's comment
 * for description.
 * 
 * 
 */
#define CV_OUTPUT_MODE_OFF 0            // disables the CV output - this saves resources - for a slightly better audio quality if not needed at all.
#define CV_OUTPUT_MODE_LOG 1            // uses a logarithmic curve for CV output (1V/Oct for Moog & Roland)
#define CV_OUTPUT_MODE_LINEAR 2         // uses a linear transfer function for CV output (819Hz/V for Korg & Yamaha)
#define CV_OUTPUT_MODE CV_OUTPUT_MODE_OFF

/*
 * GATE OUTPUT (not available on this mod)
 *
 * the GATE output port is being used to emit a square wave rendition
 * of the exact same audio output frequency to be measured
 * by a second Arduino which in turn drives the display.
 */


 
/*
 * DEBUG DDS ISR TIMING
 * 
 * this option will toggle the RED led during the ISR routine
 * to actually measure the ISR timing via an oscilloscope
 * to measure the actual ISR duration to avoid dropouts.
 * 
 */
//#define DEBUG_DDS_ISR_BY_RED_LED

/*
 * INCLUDES A PRECISE PURE SINEWAVE IF DEFINED
 * as waveform at index 0 (the very first)
 * 
 * useful for test purposes (FFT) or to be played as a very neutral timbre
 * 
 * this sinewave generates an almost pure harmonic content
 * it still will have limitations because of the limited (1024 points) table
 * and the strict timing implemented in this current OpenTheremin firmware.
 * 
 * none of standard wavetables included with OpenTheremin
 * are not perfect sinewaves, as the original Theremin timbre
 * has its own timbre. the OT wavetables mimic those original
 * sounding waveforms pretty accurately.
 * 
 */
//#define WAVEFORM_INCLUDE_PURE_SINE

/*
 *  PITCH_FIELD_MODE
 *  
 * - PITCH_FIELD_MODE_LEGACY
 *   the pitch air-field would only advance towards the pitch antenna, the pitch potentiometer
 *   will compress (shrink) or expand the playable field area. Same as original OT firmware.
 * 
 * - PITCH_FIELD_MODE_SYMMETRICAL
 *   the pitch air-field will behave as having a zero-beat position - the lowest frequency limit
 *   but it will behave as a bidirectional pitch increase/decrease, so that if moving the hand
 *   behind the zero-beat point preset via the Pitch field poti, the played pitch note
 *   will start to rise again, imitating the behaviour of analog theremins.
 * 
 */

#define PITCH_FIELD_MODE_LEGACY 0                 // legacy OpenTheremin mode
#define PITCH_FIELD_MODE_SYMMETRICAL 1            // experimental
#define PITCH_FIELD_MODE PITCH_FIELD_MODE_LEGACY


/**
 * SERIAL COMMAND COMMUNICATION PROTOCOL
 * 
 * The touch button present on the Theremin board is the only UX
 * element the user has available for custom interaction.
 * 
 * The values of the Register and Timbre potis
 * are being read continuosly 
 * 
 * The Theremin board sends out the SHORT or LONG press
 * events opcodes to the display board upon which the latter
 * handles the menu, options and configuration.
 * 
 * The Display board then 
 * 
 * the communication between the two board is implemented via UART
 */
#define STATE_CMD_CALIBRATION           0x16    // SYN
#define STATE_CMD_CALIBRATION_SUCCESS   0x06    // ACK
#define STATE_CMD_CALIBRATION_ERROR     0x15    // NAK
#define STATE_CMD_MUTE                  0x04    // EOT
#define STATE_CMD_UNMUTE                0x02    // STX
#define STATE_CMD_BUTTON_SHORT_PRESS    0x07    // BEL
#define STATE_CMD_BUTTON_LONG_PRESS     0x08    // BS

#define STATE_CMD_WAVEFORM_BASE         0x80

#define STATE_CMD_REGISTER_LOW          0x10    // DC1
#define STATE_CMD_REGISTER_MID          0x11    // DC2
#define STATE_CMD_REGISTER_HIGH         0x12    // DC3

#endif // _BUILD_COMMON_H_
