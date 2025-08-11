/**
 * @file HT1635.cpp
 * @brief implementation code for the HT1635A matrix display driver
 * 
 * (c) 2025 Luca Cipressi (lucaji.github.io)
 * 
 * GNU GPL v3 or later.
 * 
 * REVISION HISTORY
 *         AUG 2025     Luca Cipressi (lucaji.githu.io) First commit
 * 
 */

#include "HT1635.h"
#include <Wire.h>
#include <math.h>

#include "../../build_options.h"
#include "bitmap_fonts.h"
#include "display_main.h"



/**
 * @brief Convert a raw frequency to its closest equal temperament (12-TET) note name.
 * 
 * This function maps a given input frequency to the nearest musical note 
 * based on the 12-tone equal temperament system. It allows for a custom 
 * concert pitch (e.g., A4 = 440 Hz or 432 Hz) and provides both the MIDI 
 * note number and the cent deviation from the closest equal-tempered note.
 * 
 * @param freq
 *        The input frequency in Hz (must be > 0).
 * 
 * @param pitch_concert_a
 *        The reference pitch for A4 in Hz (typically 440.0f, but can be set 
 *        to alternate standards like 432.0f).
 * 
 * @param cent_drift
 *        Pointer to a float where the function stores the cent deviation 
 *        from the nearest equal-tempered note. Positive values mean the 
 *        frequency is sharp, negative means flat. Pass NULL to ignore.
 * 
 * @param midi_note
 *        Pointer to an int where the function stores the MIDI note number 
 *        (e.g., 69 for A4). Pass NULL to ignore.
 * 
 * @return const char*
 *         A pointer to a static string containing the note name and octave, 
 *         e.g., "A4", "C#3", "B-1". The string is stored in static memory 
 *         and will be overwritten by subsequent calls.
 */
const char* HT1635::frequency_to_note(float freq, float pitch_concert_a, float* cent_drift, int16_t*midi_note) {
    if (!isfinite(freq) || freq < 1.0f) {
        if (cent_drift) *cent_drift = 0;
        if (midi_note)  *midi_note  = 0;
        return "";
    }
    if (!isfinite(pitch_concert_a) || pitch_concert_a <= 0.0f) {
        pitch_concert_a = 440.0f; // safe default
    }

    static const char* note_names[] = {
        "C ", "C#", "D ", "D#", "E ", "F ", "F#", "G ", "G#", "A ", "A#", "B "
    };

    // ratio = log2(freq / A4) = ln(freq/A4) * (1/ln(2))

    const float ratio = logf(freq / pitch_concert_a) * INV_LN2;
    
    // MIDI note (A4=69) as float
    const float midi_f = 69.0f + 12.0f * ratio;

    // Cheap, sign-correct rounding to nearest int (avoids pulling in roundf).
    int16_t midi_i = (int16_t)(midi_f + (midi_f >= 0.0f ? 0.5f : -0.5f));

    // Index within octave, handles negatives safely.
    const uint8_t note_index = (uint8_t)((midi_i % 12 + 12) % 12);
    const int8_t  octave     = (int8_t)(midi_i / 12 - 1);

    // Cents drift in (-50..+50], clamp to [-49.99..+49.99] to reduce edge jitter.
    float cents = (midi_f - (float)midi_i) * 100.0f;
    if      (cents >  49.99f) cents =  49.99f;
    else if (cents < -49.99f) cents = -49.99f;

    if (cent_drift) *cent_drift = cents;
    if (midi_note)  *midi_note  = midi_i;

    // Compose "<note><octave>" into a small static buffer (thread-unsafe by design).
    // Max we expect: "A 10" (4 chars) or "C#-1" (4 chars). Buffer of 6 is plenty.
    static char result[6];
    result[0] = note_names[note_index][0];
    result[1] = note_names[note_index][1];

    // Write octave as [-1..10] typical; supports two digits and sign.
    int idx = 2;
    int8_t o = octave;
    if (o < 0) {
        result[idx++] = '-';
        o = (int8_t)(-o);
    }
    if (o >= 10) {
        result[idx++] = '1';
        o = (int8_t)(o - 10);
    }
    result[idx++] = (char)('0' + o);
    result[idx]   = '\0';

    DEBUG_PRINT("freq=");DEBUG_PRINT(freq);
    DEBUG_PRINT(" ");DEBUG_PRINT(result);DEBUG_PRINT(" ");
    DEBUG_PRINT(*cent_drift);DEBUG_PRINT(" cents MIDI=");DEBUG_PRINTLN(*midi_note);
    return result;
}

void HT1635::i2c_sendout_bitmap(uint8_t start_byte_index) {
    uint8_t i = start_byte_index;
    uint8_t ram_addr = start_byte_index * 2;  // one byte occupies two RAM nibbles

    while (i < HT_RAM_LAST_ADDRESS) {
        Wire.beginTransmission(this->_device_i2c_addr);
        Wire.write(CMD_DISPLAY_DATA_INPUT_COMMAND);
        Wire.write(ram_addr);  // start RAM address for this block

        // Determine how many bytes we can write (up to 30 bytes per transaction)
        uint8_t bytes_left = HT_RAM_LAST_ADDRESS - i;
        uint8_t bytes_to_send = (bytes_left > 30) ? 30 : bytes_left;

        for (uint8_t j = 0; j < bytes_to_send; j++) {
            Wire.write(_bitmap_buffer[i + j]);  // write 1 byte of display data
            ram_addr += 2;  // move 2 RAM addresses per byte
        }

        Wire.endTransmission();
        delay(1);

        i += bytes_to_send;
    }
}

// display OpenTermen logo fading in
void HT1635::display_startup_logo() {
	set_pwm_value(0);
    memcpy_P(_bitmap_buffer, font_open_termin_logo, sizeof(_bitmap_buffer));
	i2c_sendout_bitmap();
    for (uint8_t level = 0; level < 16; level++) {
        set_pwm_value(level);
        delay(200);
    }
	delay(200);
}


void HT1635::print_bytes(const uint8_t* str) {
	_memory_pointer = 0;
	Wire.beginTransmission(this->_device_i2c_addr);
	Wire.write(CMD_DISPLAY_DATA_INPUT_COMMAND);
	Wire.write(_memory_pointer);
	for (int i = 0; i < 30; i++) {
		_memory_pointer+=2;
		Wire.write(str[i]);
	}
	Wire.endTransmission();
	// Wire buffer is only 32 bytes, sendout is split into two transactions
	delay(1);
	Wire.beginTransmission(this->_device_i2c_addr);
	Wire.write(CMD_DISPLAY_DATA_INPUT_COMMAND);
	Wire.write(_memory_pointer);
	for (int i = 30; i < 40; i++) {
		Wire.write(str[i]);
	}
	Wire.endTransmission();
	delay(1);
}

void HT1635::update_display() {
	clear_display();
	if (_tuner_view_mode == tuner_view_mode_t::piano_view) {
        display_keyboard();
    } else {
        clear_display();
    }
}

HT1635::tuner_view_mode_t HT1635::get_tuner_view_mode() { return _tuner_view_mode; }

HT1635::tuner_view_mode_t HT1635::set_tuner_view_mode(HT1635::tuner_view_mode_t mode) { 
	_tuner_view_mode = mode; 
	update_display();
	return _tuner_view_mode;
}



/** Encode alteration as -1=flat, 0=natural, +1=sharp. */
enum class Alter : int8_t { Flat = -1, Nat = 0, Sharp = +1 };

/** Column→note label mapping (28 columns), based on your switch table. */
static const uint8_t PROGMEM kCol2NoteIdx[28] = {
	/*0..3 */ 6,0,0,0,
	/*4..7 */ 0,1,1,1,
	/*8..11*/ 1,2,2,2,
	/*12..15*/2,3,3,3,
	/*16..19*/3,4,4,4,
	/*20..23*/4,5,5,5,
	/*24..27*/5,6,6,6
};
static const int8_t PROGMEM kCol2Alter[28] = {
	/*0..3 */ +1,-1, 0,+1,
	/*4..7 */ +1,-1, 0,+1,
	/*8..11*/ +1,-1, 0,+1,
	/*12..15*/+1,-1, 0,+1,
	/*16..19*/+1,-1, 0,+1,
	/*20..23*/+1,-1, 0,+1,
	/*24..27*/+1,-1, 0,+1
};


/** Map col[0..27] to buffer index + bit mask for row 6. */
inline void col_to_buf_row6(uint8_t col, uint8_t& bufIndex, uint8_t& mask) {
	const uint8_t byte_idx = col / 8;                // 0..3
	const uint8_t bit_idx  = 7 - (col % 8);          // MSB-first
	bufIndex = byte_idx * 8u + 6u;                   // row 6 in that panel-byte
	mask     = (uint8_t)(1u << bit_idx);
}

// stores the moving cursor previous position on the virtual keyboard tuner view
static int8_t s_prev_col = -1;
static uint8_t s_prev_note_idx = 0xFF; // force first draw
static int8_t  s_prev_alt      = 99;
static int8_t  s_prev_octave   = 99;


// --- unchanged ---
void HT1635::display_keyboard() {
	memcpy_P(_bitmap_buffer, font_keyboard, sizeof(font_keyboard));
	i2c_sendout_bitmap();
	s_prev_col = -1;           // reset cursor state
	s_prev_note_idx = 0xFF;    // force label refresh
	s_prev_alt = 99;
	s_prev_octave = 99;
}


/**
 * @brief keyboard pitch and drift renderer
 * 
 */
void HT1635::display_keyboard_drift(float freq, float ref_a4) {
	// Basic sanity
	if (!isfinite(freq) || freq <= 0.0f) return;
	if (!isfinite(ref_a4) || ref_a4 <= 0.0f) ref_a4 = 440.0f;

	// Note centers across the octave (with E–F gap), plus virtual high C.
	static const uint8_t note_pixel_center[13] = {
		2, 4, 6, 8, 10, 14, 16, 18, 20, 22, 24, 26, 30
	};

	// --- pitch → semitone position relative to C4 (cheap log2)
	// C4 is 9 semitones below A4: C4 = A4 * 2^(-9/12)
	static const float A2C4    = 0.5946035575013605f;     // 2^(-9/12)

	const float ref_c4 = ref_a4 * A2C4;
	const float st_c   = 12.0f * (logf(freq / ref_c4) * INV_LN2); // semitones from C4

	// Quantize for MIDI/octave
	// Use the LOWER semitone bin for octave so it doesn't jump early at B♯
	const int base_st   = (int)floorf(st_c);      // already used below for idx/frac
	const int16_t midi_base = (int16_t)(60 + base_st); // MIDI for that lower bin
	int8_t  octave      = (int8_t)(midi_base / 12 - 1);

	// Interpolate column between adjacent note centers
	float frac          = st_c - (float)base_st;          // [0,1)
	if (frac < 0.0f) frac = 0.0f;                         // guard tiny negatives
	uint8_t idx         = (uint8_t)((base_st % 12 + 12) % 12);
	const float col_f   = note_pixel_center[idx] * (1.0f - frac)
						+ note_pixel_center[idx + 1] * frac;

	// Map to 28 usable columns and wrap cleanly
	int col = (int)(col_f + 0.5f);
	col = constrain(col, 0, 27);          // clamp to visible range 0..27

	// If cursor did not move, skip pixel writes entirely.
	if (col == s_prev_col) {
		// We still may need to refresh the label if octave/note changed across a boundary,
		// but that only happens when col changes; so safe to early out.
	} else {
		// Clear previous pixel (if any)
		if (s_prev_col >= 0 && s_prev_col <= 27) {
			uint8_t pidx, pmask;
			col_to_buf_row6((uint8_t)s_prev_col, pidx, pmask);
			_bitmap_buffer[pidx] ^= pmask;
			write_byte(pidx, _bitmap_buffer[pidx]);
		}

		// Set new pixel
		uint8_t bidx, bmask;
		col_to_buf_row6((uint8_t)col, bidx, bmask);
		_bitmap_buffer[bidx] ^= bmask;
		write_byte(bidx, _bitmap_buffer[bidx]);

		s_prev_col = (int8_t)col;
	}

	// --- Derive label (note name + optional alteration + octave micro)
	// Use small LUTs instead of a big switch; only rewrite if content actually changed.
	const uint8_t note_idx = pgm_read_byte(&kCol2NoteIdx[col]);
	const int8_t  alt_i8   = pgm_read_byte(&kCol2Alter[col]);
	const Alter   alt      = (Alter)alt_i8;

	// Clamp octave to available glyph range for font_micro_numbers (assume 0..9).
	const int8_t octave_disp = (int8_t)constrain((int)octave, 0, 9);

	const bool label_changed =
		(note_idx != s_prev_note_idx) ||
		(alt_i8   != s_prev_alt)      ||
		(octave_disp != s_prev_octave);

	if (label_changed) {
		Wire.beginTransmission(this->_device_i2c_addr);
		Wire.write(CMD_DISPLAY_DATA_INPUT_COMMAND);
		Wire.write(0x40); // start of the 5th module (as in your code)

		for (uint8_t i = 0; i < 8; ++i) {
			uint8_t buff5 = 0;

			// Note name (6 rows tall) shifted to MSBs
			if (i > 0 && i < 7) {
				uint8_t font_char = pgm_read_byte(&font_tall6_notes_condensed[note_idx][i - 1]);
				buff5 = (uint8_t)(font_char << 5);
			}

			// Alteration glyph (top rows), if any
			if (i < 3 && alt != Alter::Nat) {
				// 0: flat, 1: sharp in your original table order; map enum -> index
				const uint8_t alt_idx = (alt == Alter::Flat) ? 0u : 1u;
				uint8_t alt_char = pgm_read_byte(&font_tall6_alterations[alt_idx][i]);
				buff5 |= alt_char;
			}

			// Octave micro number (bottom-right)
			if (i > 3) {
				uint8_t oct_char = pgm_read_byte(&font_micro_numbers[octave_disp][i - 4]);
				buff5 |= oct_char;
			}

			Wire.write(buff5);
		}
		Wire.endTransmission();

		s_prev_note_idx = note_idx;
		s_prev_alt      = alt_i8;
		s_prev_octave   = octave_disp;
	}
}

void HT1635::print_drift(float drift) {
	if (_tuner_view_mode == numeric) {
		Wire.beginTransmission(this->_device_i2c_addr);
		Wire.write(CMD_DISPLAY_DATA_INPUT_COMMAND);
		Wire.write(_memory_pointer);

		// numeric cent drift
		// plus minus glyphs
		uint8_t idx = 10;
		if (drift < 0) {
			idx = 11;
			drift = -drift;
		}
		for (int y = 0; y < 5; y++) {
			uint8_t b = pgm_read_byte(&font_small_numbers[idx][y]); // this small font is made up in nibbles
			Wire.write(b);
		}
		// lower rows padding (8 - 5)
		Wire.write(0);
		Wire.write(0);
		Wire.write(0);
		// two digit cent drift
		int8_t cents = roundf(drift);
		uint8_t tens = (cents / 10) % 10;
		uint8_t units = cents % 10;
		for (int y = 0; y < 5; y++) {
			uint8_t b = pgm_read_byte(&font_small_numbers[tens][y]) << 4; 	// tens
			b |= pgm_read_byte(&font_small_numbers[units][y]);				// units
			Wire.write(b);
		}
		// lower rows padding (8 - 5)
		Wire.write(0);
		Wire.write(0);
		Wire.write(0);

	} else if (_tuner_view_mode == bar_graph) {
		Wire.beginTransmission(this->_device_i2c_addr);
		Wire.write(CMD_DISPLAY_DATA_INPUT_COMMAND);
		Wire.write(_memory_pointer);

		int8_t drift_int = roundf(drift) / 10;
		const uint8_t center = 5;
		uint16_t bar_mask = 1 << center;  // Always include the center tick
		if (drift_int > 0) {
			bar_mask |= ((1 << drift_int) - 1) << (center + 1);
		} else {
			bar_mask |= ((1 << -drift_int) - 1) << (center - (-drift_int));
		}

		// BAR GRAPH CENTS INDICATION 11 bits
		// 7654321076543210
		//0          |
		//1          |
		//2          |
		//3     *****|*****
		//4     *****|*****
		//5          |
		//6          |
		//7          |

		// display module 4
		Wire.write(0);
		Wire.write(0);
		Wire.write(0);
		Wire.write(bar_mask >> 8 & 0xff);
		Wire.write(bar_mask >> 8 & 0xff);
		Wire.write(0);
		Wire.write(0);
		Wire.write(0);

		// display module 5
		Wire.write(0x20);
		Wire.write(0x20);
		Wire.write(0x20);
		Wire.write(bar_mask & 0xff);
		Wire.write(bar_mask & 0xff);
		Wire.write(0x20);
		Wire.write(0x20);
		Wire.write(0x20);

	}
	
	Wire.endTransmission();
	delay(1);
}

void HT1635::print_string5(const char* str) {
	_memory_pointer = 0;
	Wire.beginTransmission(this->_device_i2c_addr);
	Wire.write(CMD_DISPLAY_DATA_INPUT_COMMAND);
	Wire.write(_memory_pointer);
	int pos = 0;
	while (str[pos] > 0) {
		if (pos == 3) {
			// split transaction > 32 bytes
			Wire.endTransmission();
			delay(1);
			Wire.beginTransmission(this->_device_i2c_addr);
			Wire.write(CMD_DISPLAY_DATA_INPUT_COMMAND);
			Wire.write(_memory_pointer);
		}
		for (int i = 0; i < 8; i++) {
			Wire.write(pgm_read_byte(&Font_6x8[str[pos] - 0x20][i]));
			_memory_pointer+=2;
		}
		pos++;
	}
	// Wire.write(0);
	// _memory_pointer+=2;
	Wire.endTransmission();
	delay(1);
}

void HT1635::render_pitch_and_drift(float freq, float concert_ref_a, float min_valid_freq) {
	if (freq < min_valid_freq) {
		print_string5("-    ");
		return;
	}

	if (_tuner_view_mode == HT1635::tuner_view_mode_t::piano_view) {
		display_keyboard_drift(freq, concert_ref_a);
		return;
	}
	
	float cents = 0.f;
	int16_t midi = 0;
	const char* pitch_name = frequency_to_note(freq, concert_ref_a, &cents, &midi);
	DEBUG_PRINT(pitch_name);DEBUG_PRINT(cents>=0?"+":"");DEBUG_PRINT(cents);
	
	print_string5(pitch_name);
	print_drift(cents);
}

void HT1635::print_char(char theChar, uint8_t at, bool restart) {
	if (restart) { _memory_pointer = at; }
	Wire.beginTransmission(this->_device_i2c_addr);
	Wire.write(CMD_DISPLAY_DATA_INPUT_COMMAND);
	Wire.write(_memory_pointer);
	for (int i = 0; i < 8; i++) {
		Wire.write(pgm_read_byte(&Font_6x8[theChar - 0x20][i]));
	}
	Wire.endTransmission();
	_memory_pointer+=18;
	delay(1);
}

uint8_t HT1635::write_byte(uint8_t ramByteIndex, uint8_t value) {
	_memory_pointer = ramByteIndex;
	Wire.beginTransmission(this->_device_i2c_addr);
	Wire.write(CMD_DISPLAY_DATA_INPUT_COMMAND);
	Wire.write(_memory_pointer * 2);
	Wire.write(value);
	Wire.endTransmission();
	delay(1);
	_memory_pointer+=2;
	return _memory_pointer;
}

void HT1635::clear_display(void) {
	// HT1635B has 88x4 RAM space 352bits in total, but this 
	// board leaves last 8 bits unconnected so
	// 80 bits in 4 bits for address in 320bits (8x8x5 modules)
	// for a complete ram reset and reset of the internal
	// ram index pointer, we need to zero 176 bytes
	// each byte sent to RAM at a given starting address
	// will fill the display column with the MSB to the left.
	// 44 bytes should suffice 352/8
	memset(_bitmap_buffer, 0, sizeof(_bitmap_buffer));
	uint8_t row = ParamTx(0, 0);
	row = ParamTx(0, row);
	row = ParamTx(0, row);
	row = ParamTx(0, row);
	row = ParamTx(0, row);
	_memory_pointer = 0;
}

// sents out 8 bytes
uint8_t HT1635::ParamTx(uint8_t val, uint8_t rown) {
	_memory_pointer = rown;
	Wire.beginTransmission(this->_device_i2c_addr);
	Wire.write(CMD_DISPLAY_DATA_INPUT_COMMAND);
	Wire.write(rown);
	for (int i = 0; i < 8; i++) {
		Wire.write(val);
		_memory_pointer+=2;
	}
	Wire.endTransmission();
	delay(1);
	return _memory_pointer;
}

void HT1635::UpdateRegisters(void) {
	_memory_pointer = 0;
	set_power_mode(POWER_STANDBY);
	set_blink_mode(_blink_setting);
	set_cascade_mode(_cascade_mode);
	set_com_pins_mode(_com_pins_mode);
	set_pwm_value(_pwm_setting);
	clear_display();
	set_power_mode(POWER_ON);
}

void HT1635::ResetDefaults(void) {
	_device_i2c_addr = HT1635_I2C_ADDRESS;
	_com_pins_mode = PIN_P_MOS;
	_blink_setting = BLINK_OFF;
	_cascade_mode = RC_MASTERMODE_0;
	_pwm_setting = 0x00;
	UpdateRegisters();
}

// constructor
HT1635::HT1635() {
	memset(_bitmap_buffer, 0, sizeof(_bitmap_buffer));
	_memory_pointer = 0;
}

void HT1635::begin() {
	Wire.begin();
	UpdateRegisters();
}

uint8_t HT1635::send_cmd(command_opcode_t command, uint8_t mode)  {
	Wire.beginTransmission(this->_device_i2c_addr);
	Wire.write((uint8_t)command);
	Wire.write(mode);
	uint8_t error = Wire.endTransmission();
	delay(1);
	return error;
}