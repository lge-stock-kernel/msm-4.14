#include "leds-qpnp-pattern.h"

/* Valueless constants just for indication
 */
#define PWM_PERIOD_NS                   1000000
#define LPG_NEED_TO_SET                 -1

/* Features used internally
 */
#define PATTERN_FOR_HIDDEN_MENU         true;

/* Factors for brightness tuning
 */
#define BRIGHTNESS_BASE_RGB             255
#define BRIGHTNESS_BASE_LUT             255
#define BRIGHTNESS_BASE_PCT             100

#define TUNING_LUT_SCALE                BRIGHTNESS_BASE_LUT
#define TUNING_PCT_RED                  BRIGHTNESS_BASE_PCT
#define TUNING_PCT_GREEN                BRIGHTNESS_BASE_PCT
#define TUNING_PCT_BLUE                 BRIGHTNESS_BASE_PCT

/* The brightness scale value should be in [0, BRIGHTNESS_MAX_FOR_LUT].
 *
 * The Maximum scale value depends on the maximum PWM size.
 * In the case of QPNP PMI8994, the supported PWM sizes are listed like
 *     qcom,supported-sizes = <6>, <7>, <9>;
 * and BRIGHTNESS_MAX_FOR_LUT is defined as (2^9)-1 = 511
 *
 * When it needs to define the maximum LED brightness,
 *     test it with qpnp_pattern_scale().
 * And when maximum LED brightness is defined,
 *     set the qpnp_brightness_scale to BRIGHTNESS_MAX_SCALE
 */

static int qpnp_brightness_scale = TUNING_LUT_SCALE;

extern struct qpnp_led_dev*  qpnp_led_red;
extern struct qpnp_led_dev*  qpnp_led_green;
extern struct qpnp_led_dev*  qpnp_led_blue;
extern struct qpnp_tri_led_chip* qpnp_rgb_chip;

extern struct list_head pwm_chips;
struct	pwm_output_pattern*	duty_pattern;
u64 parameter_scaled[PATTERN_SIZE_LUT] = {0, };

int qpnp_pattern_scenario_index(enum qpnp_pattern_scenario scenario)
{
	switch (scenario)
	{
	case PATTERN_SCENARIO_CALENDAR_REMIND:
	case PATTERN_SCENARIO_MISSED_NOTI_ONCE:
	case PATTERN_SCENARIO_DISNEY_NOTI_ONCE:
	case PATTERN_SCENARIO_LCD_ON:
	case PATTERN_SCENARIO_FAILED_CHECKPASSWORD:
		return 2;
	case PATTERN_SCENARIO_MISSED_NOTI:
	case PATTERN_SCENARIO_URGENT_CALL_MISSED_NOTI:
		return 4;
	case PATTERN_SCENARIO_CALL_01:
	case PATTERN_SCENARIO_INCOMING_CALL:
	case PATTERN_SCENARIO_DISNEY_INCOMING_CALL:
	case PATTERN_SCENARIO_URGENT_INCOMING_CALL:
	case PATTERN_SCENARIO_ALARM:
	case PATTERN_SCENARIO_DISNEY_ALARM:
	case PATTERN_SCENARIO_POWER_OFF:
	case PATTERN_SCENARIO_POWER_ON:
		return 1;
	case PATTERN_SCENARIO_CHARGING:
		return 6;
	case PATTERN_SCENARIO_SOUND_RECORDING:
		return 5;
	case PATTERN_SCENARIO_CHARGING_FULL:
	case PATTERN_SCENARIO_DEFAULT_OFF:
		return 0;
	case PATTERN_SCENARIO_AAT_LED_TEST:
		return 7;
	case PATTERN_SCENARIO_TMUS_MISSED_NOTI:
	case PATTERN_SCENARIO_TMUS_URGENT_CALL_MISSED_NOTI:
		return 9;
	default :
		break;
	}

	return -1;
}

static int* qpnp_pattern_scenario_parameter(enum qpnp_pattern_scenario scenario)
{
	int parameter_index = qpnp_pattern_scenario_index(scenario);

	if (parameter_index > -1)
		return qpnp_pattern_parameter[parameter_index];
	else
		return NULL;
}


static void qpnp_pattern_print(int parameter_pattern [])
{
	int     i = 0;

	printk("[RGB LED] LUT TABLE is \n");
	for (i = 0; i < PATTERN_SIZE_LUT; i++)
		printk("%d ", parameter_pattern[i]);
	printk("\n");

	printk("[RGB LED][RED] START:%d, LENGTH:%d\n",
		parameter_pattern[PATTERN_INDEX_RED_START], parameter_pattern[PATTERN_INDEX_RED_LENGTH]);
	printk("[RGB LED][GRN] START:%d, LENGTH:%d\n",
		parameter_pattern[PATTERN_INDEX_GREEN_START], parameter_pattern[PATTERN_INDEX_GREEN_LENGTH]);
	printk("[RGB LED][BLU] START:%d, LENGTH:%d\n",
		parameter_pattern[PATTERN_INDEX_BLUE_START], parameter_pattern[PATTERN_INDEX_BLUE_LENGTH]);
	printk("[RGB LED][COM] PAUSE_LO:%d, PAUSE_HI:%d, PAUSE_STEP:%d, FLAGS:%x\n",
		parameter_pattern[PATTERN_INDEX_PAUSE_LO], parameter_pattern[PATTERN_INDEX_PAUSE_HI],
		parameter_pattern[PATTERN_INDEX_PAUSE_STEP], parameter_pattern[PATTERN_INDEX_FLAGS]);
}


static int qpnp_rgb_set(struct qpnp_led_dev* led)
{
	int err;

	led->pwm_setting.pre_period_ns = PWM_PERIOD_NS;
	err = qpnp_tri_led_set(led);
	if (err) {
		pr_err("%s: failed to set qpnp_led\n", __func__);
		return err;
	}

	return 0;
}

static void qpnp_rgb_enable(void)
{
	struct qpnp_led_dev led[3];
	int err, i;

	if (qpnp_rgb_chip) {
		for (i = 0; i < qpnp_rgb_chip->num_leds; i++) {
			led[i] = qpnp_rgb_chip->leds[i];

			if (led[i].led_setting.brightness)
				led[i].pwm_dev->state.enabled = true;
			else
				led[i].pwm_dev->state.enabled = false;
		}
	} else
		goto failed_to_enable_rgb;

	if (led[0].pwm_dev->state.enabled) {
		err = led[0].pwm_dev->chip->ops->enable(led[0].pwm_dev->chip,
				led[0].pwm_dev);
		if (err) {
			pr_err("%s: failed to enable red pwm=%d\n",
					__func__, err);
			goto failed_to_enable_rgb;
		}
	}
	else
		led[0].pwm_dev->chip->ops->disable(led[0].pwm_dev->chip,
				led[0].pwm_dev);

	if (led[1].pwm_dev->state.enabled) {
		err = led[1].pwm_dev->chip->ops->enable(led[1].pwm_dev->chip,
				led[1].pwm_dev);
		if (err) {
			pr_err("%s: failed to enable green pwm=%d\n",
					__func__, err);
			goto failed_to_enable_rgb;
		}
	}
	else
		led[1].pwm_dev->chip->ops->disable(led[1].pwm_dev->chip,
				led[1].pwm_dev);

	if (led[2].pwm_dev->state.enabled) {
		err = led[2].pwm_dev->chip->ops->enable(led[2].pwm_dev->chip,
				led[2].pwm_dev);
		if (err) {
			pr_err("%s: failed to enable blue pwm=%d\n",
					__func__, err);
			goto failed_to_enable_rgb;
		}
	}
	else
		led[2].pwm_dev->chip->ops->disable(led[2].pwm_dev->chip,
				led[2].pwm_dev);

	return;

failed_to_enable_rgb:
	pr_err("%s: failed to enable rgb\n", __func__);
}

static void qpnp_set_ramp_config(struct pwm_device *pwm,
		int parameter_pattern [])
{
	int pattern_red_start       = parameter_pattern[PATTERN_INDEX_RED_START];
	int pattern_red_length      = parameter_pattern[PATTERN_INDEX_RED_LENGTH];
	int pattern_green_start     = parameter_pattern[PATTERN_INDEX_GREEN_START];
	int pattern_green_length    = parameter_pattern[PATTERN_INDEX_GREEN_LENGTH];
	int pattern_blue_start      = parameter_pattern[PATTERN_INDEX_BLUE_START];
	int pattern_blue_length     = parameter_pattern[PATTERN_INDEX_BLUE_LENGTH];

	int i;
	struct qpnp_lpg_channel* lpg;

	/* LUT config : Set R/G/B common parameters. */
	int flags = parameter_pattern[PATTERN_INDEX_FLAGS];

        if( duty_pattern == NULL )
            qpnp_pattern_config();

	if (!strncmp(pwm->label, "red", strlen("red"))) {
		lpg = qpnp_get_lpg_channel(qpnp_led_red->pwm_dev->chip, qpnp_led_red->pwm_dev);
		if (lpg == NULL)
			goto failed_to_get_lpg;

		lpg->ramp_config.pattern_length = pattern_red_length;
		lpg->ramp_config.step_ms = parameter_pattern[PATTERN_INDEX_PAUSE_STEP];
		lpg->ramp_config.lo_idx = pattern_red_start;
		lpg->ramp_config.hi_idx = pattern_red_start + pattern_red_length - 1;
		lpg->ramp_config.pause_hi_count = parameter_pattern[PATTERN_INDEX_PAUSE_HI];
		lpg->ramp_config.pause_lo_count = parameter_pattern[PATTERN_INDEX_PAUSE_LO];
		lpg->ramp_config.ramp_dir_low_to_hi = !!(flags & PM_PWM_LUT_RAMP_UP);
		lpg->ramp_config.pattern_repeat = !!(flags & PM_PWM_LUT_LOOP);
		lpg->ramp_config.toggle = !!(flags & PM_PWM_LUT_REVERSE);
		lpg->lut_written = false;

		duty_pattern->num_entries = pattern_red_length;
		duty_pattern->duty_pattern = &parameter_scaled[pattern_red_start];
		duty_pattern->cycles_per_duty = parameter_pattern[PATTERN_INDEX_PAUSE_STEP];
		qpnp_led_red->pwm_dev->state.output_pattern = duty_pattern;

		for (i = 0; i < pattern_red_length; i++)
			lpg->ramp_config.pattern[i] = (u32)parameter_scaled[pattern_red_start + i];
	} else if (!strncmp(pwm->label, "green", strlen("green"))) {
		lpg = qpnp_get_lpg_channel(qpnp_led_green->pwm_dev->chip, qpnp_led_green->pwm_dev);
		if (lpg == NULL)
			goto failed_to_get_lpg;

		lpg->ramp_config.pattern_length = pattern_green_length;
		lpg->ramp_config.step_ms = parameter_pattern[PATTERN_INDEX_PAUSE_STEP];
		lpg->ramp_config.lo_idx = pattern_green_start;
		lpg->ramp_config.hi_idx = pattern_green_start + pattern_green_length - 1;
		lpg->ramp_config.pause_hi_count = parameter_pattern[PATTERN_INDEX_PAUSE_HI];
		lpg->ramp_config.pause_lo_count = parameter_pattern[PATTERN_INDEX_PAUSE_LO];
		lpg->ramp_config.ramp_dir_low_to_hi = !!(flags & PM_PWM_LUT_RAMP_UP);
		lpg->ramp_config.pattern_repeat = !!(flags & PM_PWM_LUT_LOOP);
		lpg->ramp_config.toggle = !!(flags & PM_PWM_LUT_REVERSE);
		lpg->lut_written = false;

		duty_pattern->num_entries = pattern_green_length;
		duty_pattern->duty_pattern = &parameter_scaled[pattern_green_start];
		duty_pattern->cycles_per_duty = parameter_pattern[PATTERN_INDEX_PAUSE_STEP];
		qpnp_led_green->pwm_dev->state.output_pattern = duty_pattern;

		for (i = 0; i < pattern_green_length; i++)
			lpg->ramp_config.pattern[i] = (u32)parameter_scaled[pattern_green_start + i];
	} else if (!strncmp(pwm->label, "blue", strlen("blue"))) {
		lpg = qpnp_get_lpg_channel(qpnp_led_blue->pwm_dev->chip, qpnp_led_blue->pwm_dev);
		if (lpg == NULL)
			goto failed_to_get_lpg;

		lpg->ramp_config.pattern_length = pattern_blue_length;
		lpg->ramp_config.step_ms = parameter_pattern[PATTERN_INDEX_PAUSE_STEP];
		lpg->ramp_config.lo_idx = pattern_blue_start;
		lpg->ramp_config.hi_idx = pattern_blue_start + pattern_blue_length - 1;
		lpg->ramp_config.pause_hi_count = parameter_pattern[PATTERN_INDEX_PAUSE_HI];
		lpg->ramp_config.pause_lo_count = parameter_pattern[PATTERN_INDEX_PAUSE_LO];
		lpg->ramp_config.ramp_dir_low_to_hi = !!(flags & PM_PWM_LUT_RAMP_UP);
		lpg->ramp_config.pattern_repeat = !!(flags & PM_PWM_LUT_LOOP);
		lpg->ramp_config.toggle = !!(flags & PM_PWM_LUT_REVERSE);
		lpg->lut_written = false;

		duty_pattern->num_entries = pattern_blue_length;
		duty_pattern->duty_pattern = &parameter_scaled[pattern_blue_start];
		duty_pattern->cycles_per_duty = parameter_pattern[PATTERN_INDEX_PAUSE_STEP];
		qpnp_led_blue->pwm_dev->state.output_pattern = duty_pattern;

		for (i = 0; i < pattern_blue_length; i++)
			lpg->ramp_config.pattern[i] = (u32)parameter_scaled[pattern_blue_start + i];
	}

	return;

failed_to_get_lpg:
	pr_err("%s failed to get lpg\n", pwm->label);
}

static void qpnp_lut_init(void)
{
	struct	qpnp_lpg_channel* lpg;
	struct qpnp_led_dev* led;
	int i;

	pr_err("%s: initialize LUT pattern\n", __func__);

	if (qpnp_rgb_chip) {
		for (i = 0; i < qpnp_rgb_chip->num_leds; i++) {
			led = &qpnp_rgb_chip->leds[i];

			lpg = qpnp_get_lpg_channel(led->pwm_dev->chip, led->pwm_dev);
			if (lpg == NULL)
				goto lpg_get_error;

//			qpnp_init_lut_pattern(lpg);
		}
	}

	msleep(10);

	return;

lpg_get_error:
	pr_err("%s: failed to get lpg channel\n", __func__);
}

int qpnp_pattern_play(int parameter_pattern [])
{
	int	i               = 0;
	int     err             = 0;

	int pattern_red_start       = parameter_pattern[PATTERN_INDEX_RED_START];
	int pattern_red_length      = parameter_pattern[PATTERN_INDEX_RED_LENGTH];
	int pattern_green_start     = parameter_pattern[PATTERN_INDEX_GREEN_START];
	int pattern_green_length    = parameter_pattern[PATTERN_INDEX_GREEN_LENGTH];
	int pattern_blue_start      = parameter_pattern[PATTERN_INDEX_BLUE_START];
	int pattern_blue_length     = parameter_pattern[PATTERN_INDEX_BLUE_LENGTH];

	/* Apply scale factor to LUT(Look Up Table) entries to meet LG LED brightness guide */
	for( i=0; i<PATTERN_SIZE_LUT; i++ )
		parameter_scaled[i] = parameter_pattern[i] * qpnp_brightness_scale / BRIGHTNESS_BASE_RGB;

	/* If R/G/B share their LUT, then SKIP the individual color tuning */
	if (0 < pattern_red_length &&    // Whether red == green
		pattern_red_start==pattern_green_start && pattern_red_length==pattern_green_length )
		goto skip_color_tuning;
	if (0 < pattern_green_length &&  // Whether green == blue
		pattern_green_start==pattern_blue_start && pattern_green_length==pattern_blue_length )
		goto skip_color_tuning;
	if (0 < pattern_blue_length &&   // Whether blue == red
		pattern_blue_start==pattern_red_start && pattern_blue_length==pattern_red_length )
		goto skip_color_tuning;

	/* Apply R/G/B tuning factor to LUT(Look Up Table) entries for white balance */
	for ( i=0; i<PATTERN_SIZE_LUT; i++ ) {
		int lut_tuning;

		if (pattern_red_start<=i && i<pattern_red_start+pattern_red_length)
			lut_tuning = TUNING_PCT_RED;
		else if (pattern_green_start<=i && i<pattern_green_start+pattern_green_length)
			lut_tuning = TUNING_PCT_GREEN;
		else if (pattern_blue_start<=i && i<pattern_blue_start+pattern_blue_length)
			lut_tuning = TUNING_PCT_BLUE;
		else
			lut_tuning = 0;

		parameter_scaled[i] = (u32)parameter_scaled[i] * lut_tuning / BRIGHTNESS_BASE_PCT;
	}

skip_color_tuning:
	/* LUT config : for RED */
	qpnp_set_ramp_config(qpnp_led_red->pwm_dev, parameter_pattern);

	if (pattern_red_length > 0) {
		qpnp_led_red->led_setting.brightness = LED_FULL;
		qpnp_led_red->led_setting.breath = true;
	} else {
		qpnp_led_red->led_setting.brightness = LED_OFF;
		qpnp_led_red->led_setting.breath = false;
	}

	err = qpnp_rgb_set(qpnp_led_red);
	if (err)
		goto failed_to_play_pattern;

	/* LUT config : for GREEN */
	qpnp_set_ramp_config(qpnp_led_green->pwm_dev, parameter_pattern);

	if (pattern_green_length > 0) {
		qpnp_led_green->led_setting.brightness = LED_FULL;
		qpnp_led_green->led_setting.breath = true;
	} else {
		qpnp_led_green->led_setting.brightness = LED_OFF;
		qpnp_led_green->led_setting.breath = false;
	}

// if use green led, qpnp_rgb_set() required.
#if 0
	err = qpnp_rgb_set(qpnp_led_green);
	if (err)
		goto failed_to_play_pattern;
	usleep_range(100, 100);
#endif

	/* LUT config : for BLUE */
	qpnp_set_ramp_config(qpnp_led_blue->pwm_dev, parameter_pattern);

	if (pattern_blue_length > 0) {
		qpnp_led_blue->led_setting.brightness = LED_FULL;
		qpnp_led_blue->led_setting.breath = true;
	} else {
		qpnp_led_blue->led_setting.brightness = LED_OFF;
		qpnp_led_blue->led_setting.breath = false;
	}

// if use blue led, qpnp_rgb_set() required.
#if 0
	err = qpnp_rgb_set(qpnp_led_blue);
	if (err)
		goto failed_to_play_pattern;
	usleep_range(100, 100);
#endif

	qpnp_rgb_enable();

	return 0;

failed_to_play_pattern :
	pr_err("Failed to play pattern\n");
	return err;
}

static void qpnp_pattern_turnoff(void)
{
	int* turnoff_pattern = qpnp_pattern_scenario_parameter(PATTERN_SCENARIO_DEFAULT_OFF);

    if ( turnoff_pattern == NULL )
    	pr_err("Invalid led turnoff pattern value\n");
    else
    	qpnp_pattern_play(turnoff_pattern);
}

static ssize_t qpnp_pattern_select(const char* string_select, size_t string_size)
{
	enum qpnp_pattern_scenario select_scenario = PATTERN_SCENARIO_DEFAULT_OFF;
	int                        select_number   = 0;
	int*                       select_pattern  = NULL;

	if (sscanf(string_select, "%d", &select_number) != 1) {
		printk("[RGB LED] bad arguments\n");
		goto select_error;
	}

	select_scenario = select_number;

	select_pattern  = qpnp_pattern_scenario_parameter(select_scenario);

	if (select_pattern == NULL) {
		printk("Invalid led pattern value : %d, Turn off all LEDs\n", select_scenario);
		goto select_error;
	}

	printk("[RGB LED] Play pattern %d, (%s)\n",
		select_scenario, qpnp_pattern_scenario_name(select_scenario));

	if (select_scenario == PATTERN_SCENARIO_DEFAULT_OFF)
		qpnp_pattern_play(select_pattern);
	else {
		qpnp_pattern_play(select_pattern);
		qpnp_pattern_play(qpnp_pattern_scenario_parameter(PATTERN_SCENARIO_DEFAULT_OFF));
		//temporarily retry pattern play to avoid abnormal output
		msleep(100);
		qpnp_pattern_play(select_pattern);
	}

	return string_size;

select_error :
	qpnp_pattern_turnoff();
	return -EINVAL;
}

static ssize_t qpnp_pattern_input(const char* string_input, size_t string_size)
{
	int input_pattern[PATTERN_SIZE_ARRAY];

	if (sscanf(string_input, "%d,%d,%d,%d,%d,%d,%d,%d,"
				"%d,%d,%d,%d,%d,%d,%d,%d,"
				"%d,%d,%d,%d,%d,%d,%d,%d,"
				"%d,%d,%d,%d,%d,%d,%d,%d,"
				"%d,%d,%d,%d,%d,%d,%d,%d,"
				"%d,%d,%d,%d,%d,%d,%d,%d,"
				"%d,%d,%d,%d,%d,%d,%d,%d,%d,0x%02x",
		    &input_pattern[ 0], &input_pattern[ 1], &input_pattern[ 2],
		    &input_pattern[ 3], &input_pattern[ 4], &input_pattern[ 5],
		    &input_pattern[ 6], &input_pattern[ 7], &input_pattern[ 8],
		    &input_pattern[ 9], &input_pattern[10], &input_pattern[11],
		    &input_pattern[12], &input_pattern[13], &input_pattern[14],
		    &input_pattern[15], &input_pattern[16], &input_pattern[17],
		    &input_pattern[18], &input_pattern[19], &input_pattern[20],
		    &input_pattern[21], &input_pattern[22], &input_pattern[23],
		    &input_pattern[24], &input_pattern[25], &input_pattern[26],
		    &input_pattern[27], &input_pattern[28], &input_pattern[29],
		    &input_pattern[30], &input_pattern[31], &input_pattern[32],
		    &input_pattern[33], &input_pattern[34], &input_pattern[35],
		    &input_pattern[36], &input_pattern[37], &input_pattern[38],
		    &input_pattern[39], &input_pattern[40], &input_pattern[41],
		    &input_pattern[42], &input_pattern[43], &input_pattern[44],
		    &input_pattern[45], &input_pattern[46], &input_pattern[47],
		    &input_pattern[48], &input_pattern[49],
		    &input_pattern[50], &input_pattern[51],
		    &input_pattern[52], &input_pattern[53],
		    &input_pattern[54], &input_pattern[55], &input_pattern[56],
		    &input_pattern[57]) != PATTERN_SIZE_ARRAY) {
			    printk("[RGB LED] bad arguments ");

			    qpnp_pattern_turnoff();
			    return -EINVAL;
		    }

		    qpnp_pattern_print(input_pattern);
		    qpnp_pattern_play(input_pattern);
		    return string_size;
}

static ssize_t qpnp_pattern_blink(const char* string_blink, size_t string_size)
{
	uint blink_rgb = 0;
	int blink_on = 0;
	int blink_off = 0;

	int blink_pattern[] = {
		LED_BRIGHT, 0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,

		0, 48,
		0, 0,
		0, 0,

		LPG_NEED_TO_SET, LPG_NEED_TO_SET, PATTERN_STEP_DEFAULT,
		PATTERN_FLAG_BLINK
	};

	if (sscanf(string_blink, "0x%06x,%d,%d",
				&blink_rgb, &blink_on, &blink_off) != 3) {
		printk("[RGB LED] led_pattern_blink() bad arguments ");

		qpnp_pattern_turnoff();
		return -EINVAL;
	}

	printk("[RGB LED] rgb:%06x, on:%d, off:%d\n",
			blink_rgb, blink_on, blink_off);

	blink_pattern[2] = (0xFF & (blink_rgb >> 16));
	blink_pattern[4] = (0xFF & (blink_rgb >> 8));
	blink_pattern[6] = (0xFF & (blink_rgb));

	blink_pattern[2] =blink_pattern[2]/(LED_BRIGHT_MAX/LED_BRIGHT);
	blink_pattern[4] =blink_pattern[4]/(LED_BRIGHT_MAX/LED_BRIGHT);
	blink_pattern[6] =blink_pattern[6]/(LED_BRIGHT_MAX/LED_BRIGHT);

	blink_pattern[PATTERN_INDEX_PAUSE_LO] = blink_off/PATTERN_STEP_DEFAULT;
	blink_pattern[PATTERN_INDEX_PAUSE_HI] = blink_on/PATTERN_STEP_DEFAULT;

	qpnp_pattern_play(blink_pattern);
	qpnp_pattern_play(qpnp_pattern_scenario_parameter(PATTERN_SCENARIO_DEFAULT_OFF));
	//temporarily retry pattern play to avoid abnormal output
	msleep(100);
	qpnp_pattern_play(blink_pattern);

	return string_size;
}

#ifdef CONFIG_LEDS_LGE_EMOTIONAL
void time_on_off_blink( int ledon_time, int ledoff_time)
{

	int blink_pattern[] = {
        LED_BRIGHT, 0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,

	0, 48,
	0, 0,
	0, 0,

	LPG_NEED_TO_SET, LPG_NEED_TO_SET, LPG_NEED_TO_SET,
	PATTERN_FLAG_BLINK
	};

	if ( ledon_time < 0)
		return;

	//if offMS is zero it should led always on
	if (ledoff_time == 0){
		qpnp_pattern_play(qpnp_pattern_scenario_parameter(PATTERN_SCENARIO_CHARGING));
		printk("if offMS is zero it should led always on\n");
		return;
	}
	pr_info("RED LED on:%d, off:%d\n", ledon_time, ledoff_time);

	blink_pattern[PATTERN_INDEX_PAUSE_LO] = ledoff_time;
	blink_pattern[PATTERN_INDEX_PAUSE_HI] = ledon_time;
	blink_pattern[PATTERN_INDEX_PAUSE_STEP] = ledoff_time/50;

	qpnp_pattern_play(blink_pattern);

	return;
}

void time_on_oneshot( int ledon_time)
{

	int blink_pattern[] = {
        LED_BRIGHT, 0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,
        0,   0,   0,   0,   0,   0,   0,  0,

	0, 48,
	0, 0,
	0, 0,

	LPG_NEED_TO_SET, LPG_NEED_TO_SET, LPG_NEED_TO_SET,
	PATTERN_FLAG_ONESHOT
	};

	if ( ledon_time < 0)
		return;

	printk("RED LED on:%d\n", ledon_time);

	blink_pattern[PATTERN_INDEX_PAUSE_LO] = ledon_time/50;
	blink_pattern[PATTERN_INDEX_PAUSE_HI] = ledon_time/50;
	blink_pattern[PATTERN_INDEX_PAUSE_STEP] = ledon_time/10;

	qpnp_pattern_play(blink_pattern);

	return;
}
#endif

static ssize_t qpnp_pattern_onoff(const char* string_onoff, size_t string_size)
{
	uint onoff_rgb = 0;
	int onoff_pattern[] = {
        LED_BRIGHT, 0,
        LPG_NEED_TO_SET, 0,     // [0][1] : Solid color for RED
        0, 0,     // [2][3] : Solid color for GREEN
        0, 0,     // [4][5] : Solid color for BLUE
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        2, 2,
        4, 2,
        6, 2,
        PATTERN_PAUSE_DISABLED, PATTERN_PAUSE_DISABLED, PATTERN_PAUSE_DISABLED,
		PATTERN_FLAG_ONESHOT
	};

	if (sscanf(string_onoff, "0x%06x", &onoff_rgb) != 1) {
		printk("[RGB LED] led_pattern_onoff() bad arguments ");
		qpnp_pattern_turnoff();
		return -EINVAL;
	}
	onoff_pattern[2] = (0xFF & (onoff_rgb >> 16));
	onoff_pattern[4] = (0xFF & (onoff_rgb >> 8));
	onoff_pattern[6] = (0xFF & (onoff_rgb));

	onoff_pattern[2] =  onoff_pattern[2]/(LED_BRIGHT_MAX/LED_BRIGHT);
	onoff_pattern[4] =  onoff_pattern[4]/(LED_BRIGHT_MAX/LED_BRIGHT);
	onoff_pattern[6] =  onoff_pattern[6]/(LED_BRIGHT_MAX/LED_BRIGHT);

	printk("RGB :  %d",onoff_pattern[2]);
	qpnp_pattern_play(onoff_pattern);

	return string_size;
}

static ssize_t qpnp_pattern_scale(const char* string_scale, size_t string_size)
{
	if (sscanf(string_scale, "%d", &qpnp_brightness_scale) != 1) {
		printk("[RGB LED] qpnp_pattern_scale() bad arguments ");

		qpnp_pattern_turnoff();
		return -EINVAL;
	}

	return string_size;
}

static struct led_pattern_ops qpnp_pattern_ops = {
	.select = qpnp_pattern_select,
	.input  = qpnp_pattern_input,
	.blink  = qpnp_pattern_blink,
	.onoff  = qpnp_pattern_onoff,
	.scale  = qpnp_pattern_scale
};

static void qpnp_pattern_resister(void)
{
	lge_boot_mode_t boot_mode = LGE_BOOT_MODE_NORMAL;

	duty_pattern = kzalloc(sizeof(*duty_pattern), GFP_KERNEL);
	led_pattern_register(&qpnp_pattern_ops);

	qpnp_lut_init();

	pr_err("%s: initialize LUT pattern\n", __func__);

	boot_mode = lge_get_boot_mode();

	pr_err("%s: boot_mode=%d\n", __func__, boot_mode);

	if (boot_mode != LGE_BOOT_MODE_NORMAL) {
		pr_err("skip power on pattern\n");
		goto register_completed;
	}

	qpnp_pattern_play(qpnp_pattern_scenario_parameter(PATTERN_SCENARIO_POWER_ON));
	pr_err("%s: start RGB LED power on pattern operation\n", __func__);


register_completed:
	pr_err("%s: RGB driver is registered\n", __func__);
}



void qpnp_pattern_config(void)
{
	if (qpnp_rgb_chip)
		qpnp_pattern_resister();
}
