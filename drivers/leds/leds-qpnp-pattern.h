#ifndef __LEDS_QPNP_PATTERN_H_INCLUDED
#define __LEDS_QPNP_PATTERN_H_INCLUDED

#include "leds.h"
#include "led-pattern.h"
#include <linux/pwm.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <soc/qcom/lge/board_lge.h>

/*  Pattern data structure
 *
 *  0 ............. 48                  [LUT TABLE]
 *
 *  START       LENGTH
 *  48          49                      [RED]
 *  50          51                      [GRN]
 *  52          53                      [BLU]
 *
 *  PAUSE_LO    PAUSE_HI    PAUSE_STEP  [R/G/B COMMON]
 *  54          55          56
 *  FLAGS
 *  57
 */

// Sizes of a pattern data structure
#define PATTERN_SIZE_ARRAY          58
#define PATTERN_SIZE_LUT            48

// LED Brightness
#define LED_BRIGHT_MAX  255
#ifdef CONFIG_MACH_SDM450_CV7AS
#define LED_BRIGHT      154
#elif defined(CONFIG_MACH_SM6150_MH3)
#define LED_BRIGHT		18
#else
#define LED_BRIGHT		70
#endif

// Time parameters in MilliSec.
#define PATTERN_STEP_1MS             1
#define PATTERN_STEP_ON_50MS         5
#define PATTERN_STEP_ON_100MS        1
#define PATTERN_STEP_ON_160MS        2
#define PATTERN_STEP_ON_180MS        2
#define PATTERN_STEP_ON_235MS        2
#define PATTERN_STEP_ON_500MS        5
#define PATTERN_STEP_ON_700MS        7
#define PATTERN_STEP_ON_1000MS       10

#define PATTERN_STEP_OFF_250MS       3
#define PATTERN_STEP_OFF_500MS       5
#define PATTERN_STEP_OFF_650MS       7
#define PATTERN_STEP_OFF_2000MS      12
#define PATTERN_STEP_OFF_12000MS      80
#define PATTERN_STEP_OFF_15000MS     100
#define PATTERN_STEP_OFF_30000MS     250



#define PATTERN_STEP_DEFAULT        100
#define PATTERN_STEP_CHARGING       120
#define PATTERN_STEP_POWERONOFF     120
#define PATTERN_STEP_NOTI           50
#define PATTERN_STEP_HI_NOTI        75
#define PATTERN_STEP_FULL_CHARGING  10000
#define PATTERN_STEP_SPRINT_POWERONOFF    140
#define PATTERN_STEP_SPRINT_INCALL        160
#define PATTERN_STEP_SPRINT_INUSE         120
#define PATTERN_PAUSE_DISABLED      0
#define PATTERN_PAUSE_FULL_CHARGING 1
#define PATTERN_PAUSE_HI_NOTI          (12000 - (PATTERN_STEP_NOTI*7)) / PATTERN_STEP_NOTI
#define PATTERN_PAUSE_HI_MISSED_NOTI   (12000 - (PATTERN_STEP_NOTI*14)) / PATTERN_STEP_NOTI
#define PATTERN_PAUSE_HI_STEP_NOTI     (12000 - (PATTERN_STEP_HI_NOTI*7)) / PATTERN_STEP_HI_NOTI
#define PATTERN_PAUSE_HI_URGENT        (12000 - (PATTERN_STEP_NOTI*11)) / PATTERN_STEP_NOTI
#define PATTERN_PAUSE_HI_SECRETMODE    (12000 - (PATTERN_STEP_NOTI*23)) / PATTERN_STEP_NOTI
#define PATTERN_PAUSE_SPRINT_INUSE     (10000 - (PATTERN_STEP_SPRINT_INUSE*14)) / PATTERN_STEP_SPRINT_INUSE

// Define RGB LED ID
#define QPNP_ID_RGB_RED		    0
#define QPNP_ID_RGB_GREEN	    1
#define QPNP_ID_RGB_BLUE	    2

// Flags for Look Up Table
#define PM_PWM_LUT_LOOP		    0x01
#define PM_PWM_LUT_RAMP_UP	    0x02
#define PM_PWM_LUT_REVERSE	    0x04
#define PM_PWM_LUT_PAUSE_HI_EN	    0x08
#define PM_PWM_LUT_PAUSE_LO_EN	    0x10
#define PM_PWM_LUT_NO_TABLE	    0x20
#define PM_PWM_LUT_USE_RAW_VALUE    0x40

// Pattern flags for the LPG
//#define PATTERN_FLAG_ONESHOT        /*0x42*/ (PM_PWM_LUT_USE_RAW_VALUE|PM_PWM_LUT_RAMP_UP)
//#define PATTERN_FLAG_SOLID          /*0x43*/ (PATTERN_FLAG_ONESHOT|PM_PWM_LUT_LOOP)
//#define PATTERN_FLAG_REPEAT         /*0x4B*/ (PATTERN_FLAG_ONESHOT|PM_PWM_LUT_PAUSE_HI_EN|PM_PWM_LUT_LOOP)
//#define PATTERN_FLAG_WAVEFORM       /*0x4F*/ (PATTERN_FLAG_REPEAT|PM_PWM_LUT_REVERSE)
//#define PATTERN_FLAG_BLINK          /*0x5B*/ (PATTERN_FLAG_REPEAT|PM_PWM_LUT_PAUSE_LO_EN)
#define PATTERN_FLAG_ONESHOT        /*0x2*/ PM_PWM_LUT_RAMP_UP
#define PATTERN_FLAG_SOLID          /*0x3*/ (PATTERN_FLAG_ONESHOT|PM_PWM_LUT_LOOP)
#define PATTERN_FLAG_REPEAT         /*0xB*/ (PATTERN_FLAG_ONESHOT|PM_PWM_LUT_PAUSE_HI_EN|PM_PWM_LUT_LOOP)
#define PATTERN_FLAG_WAVEFORM       /*0xF*/ (PATTERN_FLAG_REPEAT|PM_PWM_LUT_REVERSE)
#define PATTERN_FLAG_BLINK          /*0x1B*/ (PATTERN_FLAG_REPEAT|PM_PWM_LUT_PAUSE_LO_EN)

// Index in a pattern data structure
#define PATTERN_INDEX_RED_START     PATTERN_SIZE_LUT
#define PATTERN_INDEX_RED_LENGTH    PATTERN_SIZE_LUT+1
#define PATTERN_INDEX_GREEN_START   PATTERN_SIZE_LUT+2
#define PATTERN_INDEX_GREEN_LENGTH  PATTERN_SIZE_LUT+3
#define PATTERN_INDEX_BLUE_START    PATTERN_SIZE_LUT+4
#define PATTERN_INDEX_BLUE_LENGTH   PATTERN_SIZE_LUT+5
#define PATTERN_INDEX_PAUSE_LO      PATTERN_SIZE_LUT+6
#define PATTERN_INDEX_PAUSE_HI      PATTERN_SIZE_LUT+7
#define PATTERN_INDEX_PAUSE_STEP    PATTERN_SIZE_LUT+8
#define PATTERN_INDEX_FLAGS         PATTERN_SIZE_LUT+9

// PATTERN_SCENARIO_DEFAULT_OFF (#0)
static int qpnp_pattern_default_off [] = {
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,

	0, 0,
	0, 0,
	0, 0,

	PATTERN_PAUSE_DISABLED, PATTERN_PAUSE_DISABLED, PATTERN_STEP_DEFAULT,
	PATTERN_FLAG_ONESHOT
};

// PATTERN_SCENARIO_CHARGING_FULL (#4)
static int qpnp_pattern_alwayson [] = {
	LED_BRIGHT, LED_BRIGHT, LED_BRIGHT, LED_BRIGHT, LED_BRIGHT, LED_BRIGHT, LED_BRIGHT, LED_BRIGHT,
	LED_BRIGHT, LED_BRIGHT, LED_BRIGHT, LED_BRIGHT, LED_BRIGHT, LED_BRIGHT, LED_BRIGHT, LED_BRIGHT,
	LED_BRIGHT, LED_BRIGHT, LED_BRIGHT, LED_BRIGHT, LED_BRIGHT, LED_BRIGHT, LED_BRIGHT, LED_BRIGHT,
	LED_BRIGHT, LED_BRIGHT, LED_BRIGHT, LED_BRIGHT, LED_BRIGHT, LED_BRIGHT, LED_BRIGHT, LED_BRIGHT,
	LED_BRIGHT, LED_BRIGHT, LED_BRIGHT, LED_BRIGHT, LED_BRIGHT, LED_BRIGHT, LED_BRIGHT, LED_BRIGHT,
	LED_BRIGHT, LED_BRIGHT, LED_BRIGHT, LED_BRIGHT, LED_BRIGHT, LED_BRIGHT, LED_BRIGHT, LED_BRIGHT,

	0,  48,
	0,  0,
	0,  0,

	PATTERN_PAUSE_DISABLED, PATTERN_PAUSE_DISABLED, PATTERN_STEP_FULL_CHARGING,
	PATTERN_FLAG_ONESHOT
};


// PATTERN_SCENARIO_LCD_ON (#2)
static int qpnp_pattern_oneshot[] = {
	/* GREEN/BLUE : 16 */
        LED_BRIGHT, 0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,

	0,  2,
	0,  0,
	0,  0,

	PATTERN_STEP_ON_500MS, PATTERN_STEP_OFF_500MS, PATTERN_STEP_DEFAULT,
	PATTERN_FLAG_ONESHOT
};

// PATTERN_SCENARIO_MISSED_NOTI_REPEAT_URGENT (#37)
//In case of blink, pause_lo time and pause_hi time should be changed.
static int qpnp_pattern_blink_on160ms_off2000ms[] = {
	/* RED : 14 */
        LED_BRIGHT, 0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,

	0, 40,
	0,  0,
	0,  0,

	PATTERN_STEP_ON_160MS, PATTERN_STEP_OFF_2000MS, PATTERN_STEP_DEFAULT,
	PATTERN_FLAG_BLINK
};

static int qpnp_pattern_blink_on180ms_off12000ms[] = {
	/* RED : 14 */
        LED_BRIGHT, 0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,

	0, 48,
	0,  0,
	0,  0,

	PATTERN_STEP_ON_180MS, PATTERN_STEP_OFF_12000MS, PATTERN_STEP_DEFAULT,
	PATTERN_FLAG_BLINK
};

static int qpnp_pattern_blink_on700ms_off650ms[] = {
	/* RED : 14 */
        LED_BRIGHT, 0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,

	0,  2,
	0,  0,
	0,  0,

	PATTERN_STEP_ON_700MS, PATTERN_STEP_OFF_650MS, PATTERN_STEP_DEFAULT,
	PATTERN_FLAG_BLINK
};

static int qpnp_pattern_blink_on500ms_off250ms[] = {
	/* RED : 14 */
        LED_BRIGHT, 0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,

	0,  2,
	0,  0,
	0,  0,

	PATTERN_STEP_ON_500MS, PATTERN_STEP_OFF_250MS, PATTERN_STEP_DEFAULT,
	PATTERN_FLAG_BLINK
};

static int qpnp_pattern_blink_on1000ms_off15000ms[] = {
	/* RED : 14 */
        LED_BRIGHT, 0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,

	0, 48,
	0,  0,
	0,  0,

	PATTERN_STEP_ON_1000MS, PATTERN_STEP_OFF_15000MS, PATTERN_STEP_DEFAULT,
	PATTERN_FLAG_BLINK
};

static int qpnp_pattern_blink_on250ms_off30000ms[] = {
	/* RED : 14 */
        LED_BRIGHT, 0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,

	0, 48,
	0,  0,
	0,  0,

	PATTERN_STEP_ON_235MS, PATTERN_STEP_OFF_30000MS, PATTERN_STEP_DEFAULT,
	PATTERN_FLAG_BLINK
};

/* In the hidden-menu,
 * the pattern numbers 0~15 of seek-bar are mapped to ...
 *
 *  	PATTERN_SCENARIO_DEFAULT_OFF                    = 0,
 *  	PATTERN_SCENARIO_POWER_ON                       = 1,
 *  	PATTERN_SCENARIO_LCD_ON                         = 2,
 *  	PATTERN_SCENARIO_CHARGING                       = 3,
 *  	PATTERN_SCENARIO_CHARGING_FULL                  = 4,
 *  	*PATTERN_SCENARIO_BLANK_5                       = 5,
 *  	PATTERN_SCENARIO_POWER_OFF                      = 6,
 *  	PATTERN_SCENARIO_MISSED_NOTI_REPEAT_GREEN       = 7,
 *  	*PATTERN_SCENARIO_BLANK_8                       = 8,
 *  	*PATTERN_SCENARIO_BLANK_9                       = 9,
 *  	*PATTERN_SCENARIO_BLANK_10                      = 10,
 *  	*PATTERN_SCENARIO_BLANK_11                      = 11,
 *  	*PATTERN_SCENARIO_BLANK_12                      = 12,
 *  	*PATTERN_SCENARIO_BLANK_13                      = 13,
 *  	*PATTERN_SCENARIO_BLANK_14                      = 14,
 *  	*PATTERN_SCENARIO_BLANK_15                      = 15
 */
enum qpnp_pattern_scenario {
	PATTERN_SCENARIO_DEFAULT_OFF,
	PATTERN_SCENARIO_POWER_ON,
	PATTERN_SCENARIO_LCD_ON,
	PATTERN_SCENARIO_CHARGING,
	PATTERN_SCENARIO_CHARGING_FULL,
	PATTERN_SCENARIO_CALENDAR_REMIND,
	PATTERN_SCENARIO_POWER_OFF,
	PATTERN_SCENARIO_MISSED_NOTI,
	PATTERN_SCENARIO_ALARM,
	PATTERN_SCENARIO_CALL_01,
	PATTERN_SCENARIO_SOUND_RECORDING = 34,
	PATTERN_SCENARIO_URGENT_CALL_MISSED_NOTI = 37,
	PATTERN_SCENARIO_INCOMING_CALL,
	PATTERN_SCENARIO_MISSED_NOTI_ONCE,
	PATTERN_SCENARIO_URGENT_INCOMING_CALL = 48,
	PATTERN_SCENARIO_KNOCK_ON = 103,
	PATTERN_SCENARIO_FAILED_CHECKPASSWORD,
	PATTERN_SCENARIO_DISNEY_INCOMING_CALL = 108,
	PATTERN_SCENARIO_DISNEY_ALARM,
	PATTERN_SCENARIO_DISNEY_NOTI_ONCE,
	PATTERN_SCENARIO_AAT_LED_TEST = 127,
	PATTERN_SCENARIO_TMUS_MISSED_NOTI = 207,
	PATTERN_SCENARIO_TMUS_URGENT_CALL_MISSED_NOTI = 237,
};

static int* qpnp_pattern_parameter [] = {
	qpnp_pattern_default_off,                //  0 <- LED OFF
	qpnp_pattern_alwayson,                   //  1 <- LED ALWAYS ON
	qpnp_pattern_oneshot,                    //  2 <- LED ONESHOT
	qpnp_pattern_blink_on160ms_off2000ms,               //  3 <- LED BLINK
	qpnp_pattern_blink_on180ms_off12000ms,                //  4 <- PATTERN_SCENARIO_DEFAULT_OFF
	qpnp_pattern_blink_on700ms_off650ms,                //  5 <- PATTERN_SCENARIO_DEFAULT_OFF
	qpnp_pattern_alwayson,                //  6 <- PATTERN_SCENARIO_DEFAULT_OFF
	qpnp_pattern_blink_on500ms_off250ms,                //  7 <- PATTERN_SCENARIO_DEFAULT_OFF
	qpnp_pattern_blink_on1000ms_off15000ms,                //  8 <- PATTERN_SCENARIO_DEFAULT_OFF
	qpnp_pattern_blink_on250ms_off30000ms,                //  9 <- PATTERN_SCENARIO_DEFAULT_OFF
};

static inline char* qpnp_pattern_scenario_name(enum qpnp_pattern_scenario scenario)
{
	switch (scenario)
	{
	case PATTERN_SCENARIO_DEFAULT_OFF                   :
		return "PATTERN_SCENARIO_DEFAULT_OFF";
	case PATTERN_SCENARIO_POWER_ON                      :
		return "PATTERN_SCENARIO_POWER_ON";
	case PATTERN_SCENARIO_LCD_ON                     :
		return "PATTERN_SCENARIO_LCD_ON";
	case PATTERN_SCENARIO_CHARGING                        :
		return "PATTERN_SCENARIO_CHARGING";
	case PATTERN_SCENARIO_CHARGING_FULL                      :
		return "PATTERN_SCENARIO_CHARGING_FULL";
	case PATTERN_SCENARIO_CALENDAR_REMIND                 :
		return "PATTERN_SCENARIO_CALENDAR_REMIND";
	case PATTERN_SCENARIO_POWER_OFF   :
		return "PATTERN_SCENARIO_POWER_OFF";
	case PATTERN_SCENARIO_MISSED_NOTI     :
		return "PATTERN_SCENARIO_MISSED_NOTI";
	case PATTERN_SCENARIO_ALARM      :
		return "PATTERN_SCENARIO_ALARM";
	case PATTERN_SCENARIO_CALL_01       :
		return "PATTERN_SCENARIO_CALL_01";
	case PATTERN_SCENARIO_SOUND_RECORDING       :
		return "PATTERN_SCENARIO_SOUND_RECORDING";
	case PATTERN_SCENARIO_URGENT_CALL_MISSED_NOTI     :
		return "PATTERN_SCENARIO_URGENT_CALL_MISSED_NOTI";
	case PATTERN_SCENARIO_INCOMING_CALL     :
		return "PATTERN_SCENARIO_INCOMING_CALL";
	case PATTERN_SCENARIO_MISSED_NOTI_ONCE  :
		return "PATTERN_SCENARIO_MISSED_NOTI_ONCE";
	case PATTERN_SCENARIO_URGENT_INCOMING_CALL     :
		return "PATTERN_SCENARIO_URGENT_INCOMING_CALL";
	case PATTERN_SCENARIO_KNOCK_ON        :
		return "PATTERN_SCENARIO_KNOCK_ON";
	case PATTERN_SCENARIO_FAILED_CHECKPASSWORD        :
		return "PATTERN_SCENARIO_FAILED_CHECKPASSWORD";
	case PATTERN_SCENARIO_DISNEY_INCOMING_CALL        :
		return "PATTERN_SCENARIO_DISNEY_INCOMING_CALL";
	case PATTERN_SCENARIO_DISNEY_ALARM        :
		return "PATTERN_SCENARIO_DISNEY_ALARM";
	case PATTERN_SCENARIO_DISNEY_NOTI_ONCE        :
		return "PATTERN_SCENARIO_DISNEY_NOTI_ONCE";
	case PATTERN_SCENARIO_AAT_LED_TEST        :
		return "PATTERN_SCENARIO_AAT_LED_TEST";
	case PATTERN_SCENARIO_TMUS_MISSED_NOTI        :
		return "PATTERN_SCENARIO_TMUS_MISSED_NOTI";
	case PATTERN_SCENARIO_TMUS_URGENT_CALL_MISSED_NOTI        :
		return "PATTERN_SCENARIO_TMUS_URGENT_CALL_MISSED_NOTI";
	default :
		break;
	}

	return "PATTERN_SCENARIO_NOT_DEFINED";
}

//static ssize_t qpnp_pattern_select(const char* string_format, size_t string_size);
//static ssize_t qpnp_pattern_input(const char* string_format, size_t string_size);
//static ssize_t qpnp_pattern_blink(const char* string_format, size_t string_size);
//static ssize_t qpnp_pattern_onoff(const char* string_format, size_t string_size);
//static ssize_t qpnp_pattern_scale(const char* string_format, size_t string_size);
void qpnp_pattern_config(void);
int qpnp_pattern_scenario_index(enum qpnp_pattern_scenario scenario);
int qpnp_pattern_play(int parameter_pattern []);
extern struct qpnp_lpg_channel *qpnp_get_lpg_channel(struct pwm_chip *pwm_chip,
		struct pwm_device *pwm);
extern int qpnp_tri_led_set(struct qpnp_led_dev *led);
extern void qpnp_init_lut_pattern(struct qpnp_lpg_channel *lpg);
#ifdef CONFIG_LEDS_LGE_EMOTIONAL
extern void time_on_off_blink( int ledon_time, int ledoff_time);
extern void time_on_oneshot( int ledon_time);
#endif
#endif /* __LEDS_QPNP_PATTERN_H_INCLUDED */
