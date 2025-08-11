/**
 * @file HT1635.h
 * @brief interface code for the HT1635A matrix display driver
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
#include <math.h>

#ifndef __HT1635_H__
#define __HT1635_H__

#define HT1635_I2C_ADDRESS 0x68

/**
 * @brief Customized class to interface a HT1635 LED display driver
 * 		  with a live pitch tuner renderer
 * 
 * This class implements both the HAL internals and methods and
 * the pitch / note drift renderers.
 * Due to tight integration with the I2C Wire implementation
 * and non-optimal electrical layout implemented in the current
 * display board (row and columns are swapped for each display module)
 * to optimize data writes and save resources, both translation units
 * - HT1635 HAL and Pitch Renderer - have been merged together.
 * 
 */
class HT1635 {
public:
	HT1635();

	void begin();

	/**********************
	 * PUBLIC HT1635 HAL
	 */

	void display_startup_logo();
	void display_keyboard();
	void display_keyboard_drift(float freq, float ref_a4);


	typedef enum {
		CMD_DISPLAY_DATA_INPUT_COMMAND = 0x80,
		CMD_SYSTEM_MODE = 0x82,
		CMD_BLINK_FREQUENCY = 0x84,
		CMD_COM_OPTION = 0x88,
		CMD_CASCADE = 0xA0,
		CMD_PWM_DUTY = 0xC0,
	} command_opcode_t;

	typedef enum {
		RC_MASTERMODE_0 = 0x4,
		RC_MASTERMODE_1 = 0x5,
		EXTCLK_MASTERMODE_0 = 0x6,
		EXT_CLK_MASTERMODE_1 = 0x7,
		SLAVE_MODE = 0,
	} cascade_mode_t;

	uint8_t set_cascade_mode(cascade_mode_t mode) {
		_cascade_mode = mode;
		int error = send_cmd(CMD_CASCADE, mode);
		return error;
	}

	typedef enum {
		PIN_N_MOS = 0x00,
		PIN_P_MOS = 0x01, // default in ctor
	} com_pins_mode_t;
	/**
	 * @brief  Configure Commomn Pin
	 * @param  ComPin select between PIN_N_MOS and PIN_P_MOS
	 * @retval error
	 */
	uint8_t set_com_pins_mode(com_pins_mode_t comPin) {
		_com_pins_mode = comPin;
		return send_cmd(CMD_COM_OPTION, comPin);
	}

	/**
	 * @brief  Configure PWM duty cycle
	 * @param  Pwm values between 0 and 15 are allowed
	 * @retval error
	 */
	uint8_t set_pwm_value(uint8_t pwm) {
		if (pwm > 15) { pwm = 15; }
		_pwm_setting = pwm;
		return send_cmd(CMD_PWM_DUTY, pwm);
	}

	typedef enum {
		BLINK_OFF = 0x00,
		BLINK_2Hz = 0x01,
		BLINK_1Hz = 0x02,
		BLINK_05Hz = 0x03,
	} blink_setting_t;
	/**
	 * @brief  Configure Blink mode
	 * @param  blink valid blink modes are
	 *           - BLINK_OFF
	 *           - BLINK_2Hz
	 *           - BLINK_1Hz
	 *           - BLINK_05Hz
	 * @retval error
	 */
	uint8_t set_blink_mode(blink_setting_t blink) {
		_blink_setting = blink;
		return send_cmd(CMD_BLINK_FREQUENCY, blink);
	}

	typedef enum {
		POWER_OFF = 0x00,
		POWER_STANDBY = 0x02,
		POWER_ON = 0x03,
	} power_mode_t;
	/**
	 * @brief  Set Power state of HT1635 IC
	 * @param  power valid power states are POWER_OFF, POWER_STANDBY and POWER_ON
	 * @retval error
	 */
	uint8_t set_power_mode(power_mode_t pwr) {
		_power_mode = pwr;
		return send_cmd(CMD_SYSTEM_MODE, pwr);
	}

	void clear_display(void);
	void ResetDefaults(void);

	void print_bytes(const uint8_t* str);
	void print_string5(const char* str);
	void print_drift(float drift);

	void print_char(char theChar, uint8_t at, bool restart);
	uint8_t write_byte(uint8_t ramByteIndex, uint8_t value);

	/***********************************************************
	 * PUBLIC TUNER RENDERER AND PITCH DETECTION METHODS AND INTERNALS
	 */
	const char* frequency_to_note(float freq, float pitch_concert_a, float* cent_drift, int16_t*midi_note);

	/**
	 * @brief Tuner rendering modes.
	 */
	typedef enum : uint8_t {
		numeric = 0,    /**< Note name + cents drift. */
		bar_graph,      /**< Centered bar drift view. */
		piano_view,     /**< Small keyboard with drift indicator. */

		tuner_view_mode_t_last /**< Sentinel: one past the last valid item. */
	} tuner_view_mode_t;

	/** @return The current tuner view mode. */
	HT1635::tuner_view_mode_t get_tuner_view_mode();
	/** @brief Sets the current tuner view mode
	 * 
	 * @return the actual set value
	*/
	HT1635::tuner_view_mode_t set_tuner_view_mode(HT1635::tuner_view_mode_t mode);

	/** 
	 * @brief main renderer method to display frequency and drift
	 * 
	 * Render pitch according to the current tuner view mode.
	 * 
	 */
	void render_pitch_and_drift(float freq, float concert_ref_a, float min_valid_freq);

	void update_display();

private:

	/**********************
	 * PRIVATE HT1635 HAL
	 */
	uint8_t _device_i2c_addr = 0x68; 	// default I2C address
	uint8_t _memory_pointer = 0;		// display memory index

	static const uint8_t HT_RAM_LAST_ADDRESS = 40;
	uint8_t _bitmap_buffer[40];

	// defaults
	uint8_t _pwm_setting = 0x00; 									// default PWM value
	com_pins_mode_t _com_pins_mode = PIN_P_MOS; 					// default pin mode
	blink_setting_t _blink_setting = BLINK_OFF; 					// default blink setting
	power_mode_t _power_mode = POWER_OFF; 							// default power mode
	cascade_mode_t _cascade_mode = RC_MASTERMODE_0; 				// default cascade mode

	uint8_t send_cmd(command_opcode_t command, uint8_t mode);
	uint8_t ParamTx(uint8_t val, uint8_t rown);
	void i2c_sendout_bitmap(uint8_t start = 0);
	void UpdateRegisters(void);

	/**
	 * PRIVATE TUNER RENDERER INTERNALS
	 */
	// Precompute 1/ln(2) to avoid a division on AVR.
    const float INV_LN2 = 1.4426950408889634f; // 1 / 0.69314718056
	tuner_view_mode_t _tuner_view_mode = tuner_view_mode_t::piano_view;


};

#endif /* __HT1635_H__ */
