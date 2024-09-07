/* Copyright (c) 2018-2019, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/regmap.h>
#include <linux/types.h>
#if defined(CONFIG_LGE_LEDS_PM8937)
#include <linux/delay.h>
#endif

#ifdef CONFIG_LEDS_LGE_EMOTIONAL
#include <linux/module.h>
#include "leds-qpnp-pattern.h"
#include "leds.h"
#endif

#define TRILED_REG_TYPE			0x04
#define TRILED_REG_SUBTYPE		0x05
#define TRILED_REG_EN_CTL		0x46

/* TRILED_REG_EN_CTL */
#define TRILED_EN_CTL_MASK		GENMASK(7, 5)
#define TRILED_EN_CTL_MAX_BIT		7

#define TRILED_TYPE			0x19
#define TRILED_SUBTYPE_LED3H0L12	0x02
#define TRILED_SUBTYPE_LED2H0L12	0x03
#define TRILED_SUBTYPE_LED1H2L12	0x04

#define TRILED_NUM_MAX			3

#define PWM_PERIOD_DEFAULT_NS		1000000

#ifndef CONFIG_LEDS_LGE_EMOTIONAL
struct pwm_setting {
	u64	pre_period_ns;
	u64	period_ns;
	u64	duty_ns;
};

struct led_setting {
	u64			on_ms;
	u64			off_ms;
	enum led_brightness	brightness;
	bool			blink;
	bool			breath;
};

struct qpnp_led_dev {
	struct led_classdev	cdev;
	struct pwm_device	*pwm_dev;
	struct pwm_setting	pwm_setting;
	struct led_setting	led_setting;
	struct qpnp_tri_led_chip	*chip;
	struct mutex		lock;
	const char		*label;
	const char		*default_trigger;
	u8			id;
	bool			blinking;
	bool			breathing;
#if defined(CONFIG_LGE_LEDS_PM8937)
	u8 ptrn_id;
	int charge_current;
	struct hrtimer led_off_timer;
	struct work_struct	work;
#endif
};

struct qpnp_tri_led_chip {
	struct device		*dev;
	struct regmap		*regmap;
	struct qpnp_led_dev	*leds;
	struct nvmem_device	*pbs_nvmem;
	struct mutex		bus_lock;
	int			num_leds;
	u16			reg_base;
	u8			subtype;
	u8			bitmap;
};
#endif

#ifdef CONFIG_LEDS_LGE_EMOTIONAL
struct qpnp_led_dev*	qpnp_led_red	= NULL;
struct qpnp_led_dev*	qpnp_led_green	= NULL;
struct qpnp_led_dev*	qpnp_led_blue	= NULL;
struct qpnp_tri_led_chip *qpnp_rgb_chip = NULL;
#endif

#if defined(CONFIG_LGE_LEDS_PM8937)

/**
* This pattern is written by light.c in sys/class/leds/red/pattern_id.
* Defined in android/vendor/lge/frameworks/base/core/java/com/lge/systemservice/core/LGLedRecord.java
*/
enum LG_LED_FW_PATTERN {
	ID_STOP,
	ID_POWER_ON,
	ID_LCD_ON,
	ID_CHARGING,
	ID_CHARGING_FULL,
	ID_CALENDAR_REMIND,
	ID_POWER_OFF,
	ID_MISSED_NOTI,
	ID_ALARM,
	ID_CALL_01,
	ID_SOUND_RECORDING = 34,
	ID_URGENT_CALL_MISSED_NOTI = 37,
	ID_INCOMING_CALL,
	ID_MISSED_NOTI_ONCE,
	ID_URGENT_INCOMING_CALL = 48,
	ID_KNOCK_ON = 103,
	ID_FAILED_CHECKPASSWORD,
	ID_DISNEY_INCOMING_CALL = 108,
	ID_DISNEY_ALARM,
	ID_DISNEY_NOTI_ONCE,
	ID_AAT_LED_TEST = 127,
	ID_TMUS_MISSED_NOTI = 207,
	ID_TMUS_URGENT_CALL_MISSED_NOTI = 237,
};

/**
* This pattern is defined for kernel to call pwm.
* If new pattern is need, add in heare and lg_led_patterns array.
*/
enum LG_LED_PTRNS {
	ID_LED_OFF,							/* off */
	ID_LED_ON_ALWAYS,				/* always on */
	ID_LED_ON_500ms_ONLY,			/* 500ms on =>  off  X 1 */
	ID_LED_ON_160ms_IN_4000ms,	/* 1920ms off => 160ms on => 1920ms off repeat */
	ID_LED_ON_160ms_IN_12000ms,	/* 5910ms off => 180ms on => 5910ms off repeat */
	ID_LED_ON_700ms_IN_2000ms,	/* 650ms off => 700ms on => 650ms off repeat */
	ID_LED_ON_HALF_BRIGHTNESS,	/* always on with half brightness */
	ID_LED_ON_500ms_IN_1000ms,	/* 250ms off => 500ms on => 250ms off repeat */
	ID_LED_ON_1000ms_IN_15000ms,	/* 15000ms off => 1000ms on => 15000ms off repeat */
	ID_LED_ON_235ms_IN_60000ms, 	/* 30000ms off => 235ms on => 30000ms off repeat */
	ID_LED_MAX,
};

struct patrn_data {
	u32 on_time_ms;
	u32 off_time_ms;
	u8 repeat;
 };

static const struct patrn_data lg_led_patterns[ID_LED_MAX] = {
	[ID_LED_OFF] = {.on_time_ms = 0, .off_time_ms = 1000, .repeat = 0},
	[ID_LED_ON_ALWAYS] = {.on_time_ms = 1000, .off_time_ms = 0, .repeat = 0},
	[ID_LED_ON_500ms_ONLY] = {.on_time_ms = 500, .off_time_ms = 500, .repeat = 1},
	[ID_LED_ON_160ms_IN_4000ms] = {.on_time_ms = 160, .off_time_ms = 1920, .repeat = 0},
	[ID_LED_ON_160ms_IN_12000ms] = {.on_time_ms = 180, .off_time_ms = 5910, .repeat = 0},
	[ID_LED_ON_700ms_IN_2000ms] = {.on_time_ms = 700, .off_time_ms = 650, .repeat = 0},
	[ID_LED_ON_HALF_BRIGHTNESS] = {.on_time_ms = 1000, .off_time_ms = 0, .repeat = 0},
	[ID_LED_ON_500ms_IN_1000ms] = {.on_time_ms = 500, .off_time_ms = 250, .repeat = 0},
	[ID_LED_ON_1000ms_IN_15000ms] = {.on_time_ms = 1000, .off_time_ms = 15000, .repeat = 0},
	[ID_LED_ON_235ms_IN_60000ms] = {.on_time_ms = 235, .off_time_ms = 30000, .repeat = 0},
};
#endif


static int qpnp_tri_led_read(struct qpnp_tri_led_chip *chip, u16 addr, u8 *val)
{
	int rc;
	unsigned int tmp;

	mutex_lock(&chip->bus_lock);
	rc = regmap_read(chip->regmap, chip->reg_base + addr, &tmp);
	if (rc < 0)
		dev_err(chip->dev, "Read addr 0x%x failed, rc=%d\n", addr, rc);
	else
		*val = (u8)tmp;
	mutex_unlock(&chip->bus_lock);

	return rc;
}

static int qpnp_tri_led_masked_write(struct qpnp_tri_led_chip *chip,
				u16 addr, u8 mask, u8 val)
{
	int rc;

	mutex_lock(&chip->bus_lock);
	rc = regmap_update_bits(chip->regmap, chip->reg_base + addr, mask, val);
	if (rc < 0)
		dev_err(chip->dev, "Update addr 0x%x to val 0x%x with mask 0x%x failed, rc=%d\n",
					addr, val, mask, rc);
	mutex_unlock(&chip->bus_lock);

	return rc;
}

static int __tri_led_config_pwm(struct qpnp_led_dev *led,
				struct pwm_setting *pwm)
{
	struct pwm_state pstate;
	int rc;

	pwm_get_state(led->pwm_dev, &pstate);
	pstate.enabled = !!(pwm->duty_ns != 0);
	pstate.period = pwm->period_ns;
	pstate.duty_cycle = pwm->duty_ns;
	pstate.output_type = led->led_setting.breath ?
		PWM_OUTPUT_MODULATED : PWM_OUTPUT_FIXED;
#ifdef CONFIG_LEDS_LGE_EMOTIONAL
	pstate.output_pattern = led->pwm_dev->state.output_pattern;
#else
	/* Use default pattern in PWM device */
	pstate.output_pattern = NULL;
#endif
	rc = pwm_apply_state(led->pwm_dev, &pstate);

	if (rc < 0)
		dev_err(led->chip->dev, "Apply PWM state for %s led failed, rc=%d\n",
					led->cdev.name, rc);

	return rc;
}

#define PBS_ENABLE	1
#define PBS_DISABLE	2
#define PBS_ARG		0x42
#define PBS_TRIG_CLR	0xE6
#define PBS_TRIG_SET	0xE5
static int __tri_led_set(struct qpnp_led_dev *led)
{
	int rc = 0;
	u8 val = 0, mask = 0, pbs_val;
	u8 prev_bitmap;

	rc = __tri_led_config_pwm(led, &led->pwm_setting);
	if (rc < 0) {
		dev_err(led->chip->dev, "Configure PWM for %s led failed, rc=%d\n",
					led->cdev.name, rc);
		return rc;
	}

	mask |= 1 << (TRILED_EN_CTL_MAX_BIT - led->id);

	if (led->pwm_setting.duty_ns == 0)
		val = 0;
	else
		val = mask;

	if (led->chip->subtype == TRILED_SUBTYPE_LED2H0L12 &&
		led->chip->pbs_nvmem) {
		/*
		 * Control BOB_CONFIG_EXT_CTRL2_FORCE_EN for HR_LED through
		 * PBS trigger. PBS trigger for enable happens if any one of
		 * LEDs are turned on. PBS trigger for disable happens only
		 * if both LEDs are turned off.
		 */

		prev_bitmap = led->chip->bitmap;
		if (val)
			led->chip->bitmap |= (1 << led->id);
		else
			led->chip->bitmap &= ~(1 << led->id);

		if (!(led->chip->bitmap & prev_bitmap)) {
			pbs_val = led->chip->bitmap ? PBS_ENABLE : PBS_DISABLE;
			rc = nvmem_device_write(led->chip->pbs_nvmem, PBS_ARG,
				1, &pbs_val);
			if (rc < 0) {
				dev_err(led->chip->dev, "Couldn't set PBS_ARG, rc=%d\n",
					rc);
				return rc;
			}

			pbs_val = 1;
			rc = nvmem_device_write(led->chip->pbs_nvmem,
				PBS_TRIG_CLR, 1, &pbs_val);
			if (rc < 0) {
				dev_err(led->chip->dev, "Couldn't set PBS_TRIG_CLR, rc=%d\n",
					rc);
				return rc;
			}

			pbs_val = 1;
			rc = nvmem_device_write(led->chip->pbs_nvmem,
				PBS_TRIG_SET, 1, &pbs_val);
			if (rc < 0) {
				dev_err(led->chip->dev, "Couldn't set PBS_TRIG_SET, rc=%d\n",
					rc);
				return rc;
			}
		}
	}

	rc = qpnp_tri_led_masked_write(led->chip, TRILED_REG_EN_CTL,
							mask, val);
	if (rc < 0)
		dev_err(led->chip->dev, "Update addr 0x%x failed, rc=%d\n",
					TRILED_REG_EN_CTL, rc);

	return rc;
}

#ifdef CONFIG_LEDS_LGE_EMOTIONAL
int qpnp_tri_led_set(struct qpnp_led_dev *led)
#else
static int qpnp_tri_led_set(struct qpnp_led_dev *led)
#endif
{
	u64 on_ms, off_ms, period_ns, duty_ns;
	enum led_brightness brightness = led->led_setting.brightness;
	int rc = 0;

	if (led->led_setting.blink) {
		on_ms = led->led_setting.on_ms;
		off_ms = led->led_setting.off_ms;

		duty_ns = on_ms * NSEC_PER_MSEC;
		period_ns = (on_ms + off_ms) * NSEC_PER_MSEC;

		if (period_ns < duty_ns && duty_ns != 0)
			period_ns = duty_ns + 1;
	} else {
		/* Use initial period if no blinking is required */
		period_ns = led->pwm_setting.pre_period_ns;

		if (brightness == LED_OFF)
			duty_ns = 0;

		duty_ns = period_ns * brightness;
		do_div(duty_ns, LED_FULL);

		if (period_ns < duty_ns && duty_ns != 0)
			period_ns = duty_ns + 1;
	}

#ifdef CONFIG_LEDS_LGE_EMOTIONAL
	dev_err(led->chip->dev, "PWM settings for %s led: period = %lluns, duty = %lluns\n",
				led->cdev.name, period_ns, duty_ns);
#else
	dev_dbg(led->chip->dev, "PWM settings for %s led: period = %lluns, duty = %lluns\n",
				led->cdev.name, period_ns, duty_ns);
#endif

	led->pwm_setting.duty_ns = duty_ns;
	led->pwm_setting.period_ns = period_ns;

	rc = __tri_led_set(led);
	if (rc < 0) {
		dev_err(led->chip->dev, "__tri_led_set %s failed, rc=%d\n",
				led->cdev.name, rc);
		return rc;
	}

	if (led->led_setting.blink) {
		led->cdev.brightness = LED_FULL;
		led->blinking = true;
		led->breathing = false;
	} else if (led->led_setting.breath) {
		led->cdev.brightness = LED_FULL;
		led->blinking = false;
		led->breathing = true;
	} else {
		led->cdev.brightness = led->led_setting.brightness;
		led->blinking = false;
		led->breathing = false;
	}

	return rc;
}

static int qpnp_tri_led_set_brightness(struct led_classdev *led_cdev,
		enum led_brightness brightness)
{
	struct qpnp_led_dev *led =
		container_of(led_cdev, struct qpnp_led_dev, cdev);
	int rc = 0;

	mutex_lock(&led->lock);
	if (brightness > LED_FULL)
		brightness = LED_FULL;

	if (brightness == led->led_setting.brightness &&
			!led->blinking && !led->breathing) {
		mutex_unlock(&led->lock);
		return 0;
	}
#ifdef CONFIG_LEDS_LGE_EMOTIONAL
        if ( LED_BRIGHT <= brightness )
            led->led_setting.brightness = LED_BRIGHT;
        else
            led->led_setting.brightness = brightness;
#else
	led->led_setting.brightness = brightness;
#endif
	if (!!brightness)
		led->led_setting.off_ms = 0;
	else
		led->led_setting.on_ms = 0;
	led->led_setting.blink = false;
	led->led_setting.breath = false;

	rc = qpnp_tri_led_set(led);
	if (rc)
		dev_err(led->chip->dev, "Set led failed for %s, rc=%d\n",
				led->label, rc);

	mutex_unlock(&led->lock);

	return rc;
}

static enum led_brightness qpnp_tri_led_get_brightness(
			struct led_classdev *led_cdev)
{
	return led_cdev->brightness;
}

static int qpnp_tri_led_set_blink(struct led_classdev *led_cdev,
		unsigned long *on_ms, unsigned long *off_ms)
{
	struct qpnp_led_dev *led =
		container_of(led_cdev, struct qpnp_led_dev, cdev);
	int rc = 0;

	mutex_lock(&led->lock);
	if (led->blinking && *on_ms == led->led_setting.on_ms &&
			*off_ms == led->led_setting.off_ms) {
		dev_dbg(led_cdev->dev, "Ignore, on/off setting is not changed: on %lums, off %lums\n",
						*on_ms, *off_ms);
		mutex_unlock(&led->lock);
		return 0;
	}

	if (*on_ms == 0) {
		led->led_setting.blink = false;
		led->led_setting.breath = false;
		led->led_setting.brightness = LED_OFF;
	} else if (*off_ms == 0) {
		led->led_setting.blink = false;
		led->led_setting.breath = false;
		led->led_setting.brightness = led->cdev.brightness;
	} else {
		led->led_setting.on_ms = *on_ms;
		led->led_setting.off_ms = *off_ms;
		led->led_setting.blink = true;
		led->led_setting.breath = false;
	}

	rc = qpnp_tri_led_set(led);
	if (rc)
		dev_err(led->chip->dev, "Set led failed for %s, rc=%d\n",
				led->label, rc);

	mutex_unlock(&led->lock);
	return rc;
}

static ssize_t breath_show(struct device *dev, struct device_attribute *attr,
							char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct qpnp_led_dev *led =
		container_of(led_cdev, struct qpnp_led_dev, cdev);

	return snprintf(buf, PAGE_SIZE, "%d\n", led->led_setting.breath);
}

static ssize_t breath_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t count)
{
	int rc;
	bool breath;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct qpnp_led_dev *led =
		container_of(led_cdev, struct qpnp_led_dev, cdev);

	rc = kstrtobool(buf, &breath);
	if (rc < 0)
		return rc;

	cancel_work_sync(&led_cdev->set_brightness_work);

	mutex_lock(&led->lock);
	if (led->breathing == breath)
		goto unlock;

	led->led_setting.blink = false;
	led->led_setting.breath = breath;
	led->led_setting.brightness = breath ? LED_FULL : LED_OFF;
	rc = qpnp_tri_led_set(led);
	if (rc < 0)
		dev_err(led->chip->dev, "Set led failed for %s, rc=%d\n",
				led->label, rc);

unlock:
	mutex_unlock(&led->lock);
	return (rc < 0) ? rc : count;
}

static DEVICE_ATTR(breath, 0644, breath_show, breath_store);
static const struct attribute *breath_attrs[] = {
	&dev_attr_breath.attr,
	NULL
};
#ifdef CONFIG_LEDS_LGE_EMOTIONAL
static int* qpnp_led_pattern_scenario_parameter(enum qpnp_pattern_scenario scenario)
{
	int parameter_index = qpnp_pattern_scenario_index(scenario);
	pr_err("scenario :  %d pattern_id : %d\n",scenario, parameter_index);
	if (parameter_index > -1)
		return qpnp_pattern_parameter[parameter_index];
	else
		return NULL;
}

static void qpnp_pattern_scenario_pattern_play(enum qpnp_pattern_scenario scenario)
{
    int* play_pattern = qpnp_led_pattern_scenario_parameter(scenario);

    if ( play_pattern == NULL )
        pr_err("Invalid led pattern value : %d\n", scenario);
    else
        qpnp_pattern_play(play_pattern);
}
#endif
#if defined(CONFIG_LGE_LEDS_PM8937)
static ssize_t pattern_id_store(struct device *dev,
	struct device_attribute *attr,
	const char *buf, size_t count)
{

	u8 pattern_id;
	ssize_t ret = -EINVAL;

	ret = kstrtou8(buf, 10, &pattern_id);
	if (ret)
		return -EINVAL;

	pr_info("pattern_id : %d\n", pattern_id);
#ifdef CONFIG_LEDS_LGE_EMOTIONAL
	qpnp_pattern_scenario_pattern_play(pattern_id);
#endif
        return count;
}

static ssize_t pattern_id_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct qpnp_led_dev *led;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	led = container_of(led_cdev, struct qpnp_led_dev, cdev);
	return sprintf(buf,"%d\n",led->ptrn_id);
}

static void qpnp_led_off_work(struct work_struct *work)
{
	struct qpnp_led_dev *led = container_of(work,
					struct qpnp_led_dev, work);
	int rc = 0;

	led->ptrn_id = ID_LED_OFF;

	led->led_setting.brightness = LED_OFF;

	led->led_setting.blink = true;
	led->led_setting.breath = false;

	led->led_setting.on_ms = lg_led_patterns[led->ptrn_id].on_time_ms;
	led->led_setting.off_ms = lg_led_patterns[led->ptrn_id].off_time_ms;

	rc = __tri_led_set(led);
	if (rc < 0) {
		dev_err(led->chip->dev, "__tri_led_set %s failed, rc=%d\n",
				led->cdev.name, rc);
	}
}

static u32 led_on_ms;
static ssize_t time_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf,"%d\n",led_on_ms);
}

static ssize_t time_store(struct device *dev,
	struct device_attribute *attr,
	const char *buf, size_t count)
{
	ssize_t ret = -EINVAL;
#ifndef CONFIG_LEDS_LGE_EMOTIONAL
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	enum led_brightness	brightness;
#endif
	u32 on_ms, off_ms;

	ret = kstrtou32(buf, 10, &led_on_ms);
        if (ret)
            return -EINVAL;

	on_ms = led_on_ms;
	off_ms = led_on_ms;

	pr_info("on_ms : %d, off_ms : %d\n", led_on_ms, led_on_ms);
#ifdef CONFIG_LEDS_LGE_EMOTIONAL
	time_on_oneshot( on_ms );
#else
        if (on_ms == 0)
            brightness = LED_OFF;
        else
            brightness = LED_FULL;

        led_set_brightness(led_cdev, brightness);
#endif
	return count;

}

static unsigned long onms, offms;
static ssize_t time_on_off_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf,"on : %lX off : %lX\n",onms, offms);
}

static ssize_t time_on_off_store(struct device *dev,
	struct device_attribute *attr,
	const char *buf, size_t count)
{
	ssize_t ret = -EINVAL;
	unsigned long on_ms, off_ms;
#ifndef CONFIG_LEDS_LGE_EMOTIONAL
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	enum led_brightness	brightness;
#endif
	ret = sscanf(buf, "%lX,%lX", &onms, &offms);

	if (ret!=2) {
		pr_err("bad parameter\n");
		return -EINVAL;
	}

	on_ms = onms;
	off_ms = offms;

	pr_info("on_ms : %d, off_ms : %d\n", on_ms, off_ms);
#ifdef CONFIG_LEDS_LGE_EMOTIONAL
	time_on_off_blink( on_ms, off_ms);
#else
        if (on_ms == 0)
            brightness = LED_OFF;
        else
            brightness = LED_FULL;

	led_blink_set(led_cdev, &on_ms, &off_ms);
#endif
	return count;
}

static enum hrtimer_restart led_off_timer_func(struct hrtimer *timer)
{
	struct qpnp_led_dev *led =
		container_of(timer, struct qpnp_led_dev, led_off_timer);
	schedule_work(&led->work);
	pr_info("LED off by timer.\n");
	return HRTIMER_NORESTART;
}
static DEVICE_ATTR(pattern_id, 0644, pattern_id_show, pattern_id_store);
static DEVICE_ATTR(timed, 0644, time_show, time_store);
static DEVICE_ATTR(time_on_off, 0644, time_on_off_show, time_on_off_store);

static struct attribute *red_attrs[] = {
	&dev_attr_pattern_id.attr,
	&dev_attr_timed.attr,
	&dev_attr_time_on_off.attr,
	NULL
};

static const struct attribute_group red_attr_group = {
	.attrs = red_attrs,
};
#if (0)
static DEVICE_ATTR(pattern_id, 0644, pattern_id_show, pattern_id_store);

static struct attribute *red_attrs[] = {
	&dev_attr_pattern_id.attr,
	NULL
};

static const struct attribute_group red_attr_group = {
	.attrs = red_attrs,
};
#endif
#endif

static int qpnp_tri_led_register(struct qpnp_tri_led_chip *chip)
{
	struct qpnp_led_dev *led;
	int rc, i, j;

	for (i = 0; i < chip->num_leds; i++) {
		led = &chip->leds[i];
		mutex_init(&led->lock);
		led->cdev.name = led->label;
		led->cdev.max_brightness = LED_FULL;
		led->cdev.brightness_set_blocking = qpnp_tri_led_set_brightness;
		led->cdev.brightness_get = qpnp_tri_led_get_brightness;
		led->cdev.blink_set = qpnp_tri_led_set_blink;
		led->cdev.default_trigger = led->default_trigger;
		led->cdev.brightness = LED_OFF;
		led->cdev.flags |= LED_KEEP_TRIGGER;

		rc = devm_led_classdev_register(chip->dev, &led->cdev);
		if (rc < 0) {
			dev_err(chip->dev, "%s led class device registering failed, rc=%d\n",
							led->label, rc);
			goto err_out;
		}

		if (pwm_get_output_type_supported(led->pwm_dev)
				& PWM_OUTPUT_MODULATED) {
			rc = sysfs_create_files(&led->cdev.dev->kobj,
					breath_attrs);
			if (rc < 0) {
				dev_err(chip->dev, "Create breath file for %s led failed, rc=%d\n",
						led->label, rc);
				goto err_out;
			}
		}

#ifdef CONFIG_LEDS_LGE_EMOTIONAL
		if (!strncmp(led->cdev.name, "red", strlen("red"))) {
			qpnp_led_red = &chip->leds[0];
		} else if (!strncmp(led->cdev.name, "green", strlen("green"))) {
			qpnp_led_green = &chip->leds[1];
		} else if (!strncmp(led->cdev.name, "blue", strlen("blue"))) {
			qpnp_led_blue = &chip->leds[2];
		}
#endif
	}

#ifdef CONFIG_LEDS_LGE_EMOTIONAL
	qpnp_rgb_chip = chip;
#endif

#if defined(CONFIG_LGE_LEDS_PM8937)
        led = &chip->leds[0];

        INIT_WORK(&led->work, qpnp_led_off_work);

        rc = sysfs_create_group(&led->cdev.dev->kobj,
                &red_attr_group);
        if (rc)
            goto err_out;

        led->ptrn_id = 0;
        hrtimer_init(&led->led_off_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
        led->led_off_timer.function = led_off_timer_func; 
#endif

	return 0;

err_out:
	for (j = 0; j <= i; j++) {
		if (j < i)
			sysfs_remove_files(&chip->leds[j].cdev.dev->kobj,
					breath_attrs);
		mutex_destroy(&chip->leds[j].lock);
	}
	return rc;
}

static int qpnp_tri_led_hw_init(struct qpnp_tri_led_chip *chip)
{
	int rc = 0;
	u8 val;

	rc = qpnp_tri_led_read(chip, TRILED_REG_TYPE, &val);
	if (rc < 0) {
		dev_err(chip->dev, "Read REG_TYPE failed, rc=%d\n", rc);
		return rc;
	}

	if (val != TRILED_TYPE) {
		dev_err(chip->dev, "invalid subtype(%d)\n", val);
		return -ENODEV;
	}

	rc = qpnp_tri_led_read(chip, TRILED_REG_SUBTYPE, &val);
	if (rc < 0) {
		dev_err(chip->dev, "Read REG_SUBTYPE failed, rc=%d\n", rc);
		return rc;
	}

	chip->subtype = val;

	return 0;
}

static int qpnp_tri_led_parse_dt(struct qpnp_tri_led_chip *chip)
{
	struct device_node *node = chip->dev->of_node, *child_node;
	struct qpnp_led_dev *led;
	struct pwm_args pargs;
	const __be32 *addr;
	int rc = 0, id, i = 0;

	addr = of_get_address(chip->dev->of_node, 0, NULL, NULL);
	if (!addr) {
		dev_err(chip->dev, "Getting address failed\n");
		return -EINVAL;
	}
	chip->reg_base = be32_to_cpu(addr[0]);

	chip->num_leds = of_get_available_child_count(node);
	if (chip->num_leds == 0) {
		dev_err(chip->dev, "No led child node defined\n");
		return -ENODEV;
	}

	if (chip->num_leds > TRILED_NUM_MAX) {
		dev_err(chip->dev, "can't support %d leds(max %d)\n",
				chip->num_leds, TRILED_NUM_MAX);
		return -EINVAL;
	}

	if (of_find_property(chip->dev->of_node, "nvmem", NULL)) {
		chip->pbs_nvmem = devm_nvmem_device_get(chip->dev, "pbs_sdam");
		if (IS_ERR_OR_NULL(chip->pbs_nvmem)) {
			rc = PTR_ERR(chip->pbs_nvmem);
			if (rc != -EPROBE_DEFER) {
				dev_err(chip->dev, "Couldn't get nvmem device, rc=%d\n",
					rc);
				return -ENODEV;
			}
			chip->pbs_nvmem = NULL;
			return rc;
		}
	}

	chip->leds = devm_kcalloc(chip->dev, chip->num_leds,
			sizeof(struct qpnp_led_dev), GFP_KERNEL);
	if (!chip->leds)
		return -ENOMEM;

	for_each_available_child_of_node(node, child_node) {
		rc = of_property_read_u32(child_node, "led-sources", &id);
		if (rc) {
			dev_err(chip->dev, "Get led-sources failed, rc=%d\n",
							rc);
			return rc;
		}

		if (id >= TRILED_NUM_MAX) {
			dev_err(chip->dev, "only support 0~%d current source\n",
					TRILED_NUM_MAX - 1);
			return -EINVAL;
		}

		led = &chip->leds[i++];
		led->chip = chip;
		led->id = id;
		led->label =
			of_get_property(child_node, "label", NULL) ? :
							child_node->name;

		led->pwm_dev =
			devm_of_pwm_get(chip->dev, child_node, NULL);
		if (IS_ERR(led->pwm_dev)) {
			rc = PTR_ERR(led->pwm_dev);
			if (rc != -EPROBE_DEFER)
				dev_err(chip->dev, "Get pwm device for %s led failed, rc=%d\n",
							led->label, rc);
			return rc;
		}

		pwm_get_args(led->pwm_dev, &pargs);
		if (pargs.period == 0)
			led->pwm_setting.pre_period_ns = PWM_PERIOD_DEFAULT_NS;
		else
			led->pwm_setting.pre_period_ns = pargs.period;

		led->default_trigger = of_get_property(child_node,
				"linux,default-trigger", NULL);
	}

	return rc;
}

#ifdef CONFIG_LEDS_LGE_EMOTIONAL
static void qpnp_leds_init(struct work_struct *work) {
	qpnp_pattern_config();
}
#endif

static int qpnp_tri_led_probe(struct platform_device *pdev)
{
	struct qpnp_tri_led_chip *chip;
	int rc = 0;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &pdev->dev;
	chip->regmap = dev_get_regmap(chip->dev->parent, NULL);
	if (!chip->regmap) {
		dev_err(chip->dev, "Getting regmap failed\n");
		return -EINVAL;
	}

	rc = qpnp_tri_led_parse_dt(chip);
	if (rc < 0) {
		dev_err(chip->dev, "Devicetree properties parsing failed, rc=%d\n",
								rc);
		return rc;
	}

	mutex_init(&chip->bus_lock);

	rc = qpnp_tri_led_hw_init(chip);
	if (rc) {
		dev_err(chip->dev, "HW initialization failed, rc=%d\n", rc);
		goto destroy;
	}

#ifdef CONFIG_LEDS_LGE_EMOTIONAL
	INIT_DELAYED_WORK(&chip->init_leds, qpnp_leds_init);
#endif
	dev_set_drvdata(chip->dev, chip);
	rc = qpnp_tri_led_register(chip);
	if (rc < 0) {
		dev_err(chip->dev, "Registering LED class devices failed, rc=%d\n",
								rc);
		goto destroy;
	}

	dev_dbg(chip->dev, "Tri-led module with subtype 0x%x is detected\n",
					chip->subtype);

#ifdef CONFIG_LEDS_LGE_EMOTIONAL
	if (qpnp_led_red && qpnp_led_green && qpnp_led_blue) {
		schedule_delayed_work(&chip->init_leds,
				msecs_to_jiffies(8000));
	}
#endif

	return 0;
destroy:
	mutex_destroy(&chip->bus_lock);
	dev_set_drvdata(chip->dev, NULL);

	return rc;
}

static int qpnp_tri_led_remove(struct platform_device *pdev)
{
	int i;
	struct qpnp_tri_led_chip *chip = dev_get_drvdata(&pdev->dev);

	mutex_destroy(&chip->bus_lock);
	for (i = 0; i < chip->num_leds; i++) {
		sysfs_remove_files(&chip->leds[i].cdev.dev->kobj, breath_attrs);
		mutex_destroy(&chip->leds[i].lock);
	}
	dev_set_drvdata(chip->dev, NULL);
	return 0;
}

static const struct of_device_id qpnp_tri_led_of_match[] = {
	{ .compatible = "qcom,tri-led",},
	{ },
};

static struct platform_driver qpnp_tri_led_driver = {
	.driver		= {
		.name		= "qcom,tri-led",
		.of_match_table	= qpnp_tri_led_of_match,
	},
	.probe		= qpnp_tri_led_probe,
	.remove		= qpnp_tri_led_remove,
};
module_platform_driver(qpnp_tri_led_driver);

MODULE_DESCRIPTION("QTI TRI_LED driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("leds:qpnp-tri-led");
