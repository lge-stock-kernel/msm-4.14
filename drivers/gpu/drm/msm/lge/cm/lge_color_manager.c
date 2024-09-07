/*
 * Copyright(c) 2017, LG Electronics. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt)     "[Display][lge-cm:%s:%d] " fmt, __func__, __LINE__

#include <linux/kallsyms.h>
#include "lge_color_manager.h"
#include "lge_dsi_panel_def.h"
#include "lge_dsi_panel.h"
#include "dsi_display.h"
#include <linux/delay.h>

static ssize_t sharpness_get(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dsi_panel *panel;

	panel = dev_get_drvdata(dev);
	if (panel == NULL) {
		pr_err("panel is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", panel->lge.sharpness);
}

static ssize_t sharpness_set(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	ssize_t ret = strnlen(buf, PAGE_SIZE);
	struct dsi_panel *panel;
	int input;

	panel = dev_get_drvdata(dev);
	if (panel == NULL) {
		pr_err("panel is NULL\n");
		return -EINVAL;
	}

	if (panel->lge.ddic_ops == NULL) {
		pr_err("panel ops is NULL\n");
		return -EINVAL;
	}

	if(!dsi_panel_initialized(panel)) {
		pr_err("panel not yet initialized\n");
		return -EINVAL;
	}

	sscanf(buf, "%d", &input);
	panel->lge.sharpness = input;
	pr_info("ctrl->sharpness (%d)\n", panel->lge.sharpness);

	if (panel->lge.ddic_ops->sharpness_set)
		panel->lge.ddic_ops->sharpness_set(panel, input);
	else
		pr_err("Can not find sharpness_set\n");
	return ret;
}

static DEVICE_ATTR(sharpness, S_IRUGO | S_IWUSR | S_IWGRP,
		sharpness_get, sharpness_set);

static ssize_t screen_mode_get(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct dsi_panel *panel;
	int len = 0;

	panel = dev_get_drvdata(dev);
	if (!panel) {
		pr_err("panel is NULL\n");
		return len;
	}

	return sprintf(buf, "%d\n", panel->lge.screen_mode);
}

static ssize_t screen_mode_set(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	ssize_t ret = strnlen(buf, PAGE_SIZE);
	struct dsi_panel *panel;
	int input;

	panel = dev_get_drvdata(dev);
	if (panel == NULL) {
		pr_err("panel is NULL\n");
		return -EINVAL;
	}

	if (!dsi_panel_initialized(panel)) {
		pr_err("Panel off state. Ignore screen_mode set cmd\n");
		return -EINVAL;
	}

	sscanf(buf, "%d", &input);
	panel->lge.screen_mode = input;

	pr_info("ctrl->screen_mode (%d)\n", panel->lge.screen_mode);

	if (panel->lge.ddic_ops && panel->lge.ddic_ops->lge_set_screen_mode)
		panel->lge.ddic_ops->lge_set_screen_mode(panel, true);
	else
		pr_err("Can not find lge_set_screen_mode\n");

	return ret;
}
static DEVICE_ATTR(screen_mode, S_IRUGO | S_IWUSR | S_IWGRP,
					screen_mode_get, screen_mode_set);

static ssize_t screen_tune_get(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct dsi_panel *panel;

	panel = dev_get_drvdata(dev);
	if (!panel) {
		pr_err("panel is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d %d %d\n", panel->lge.sc_sat_step,
					panel->lge.sc_hue_step,
					panel->lge.sc_sha_step);
}

static ssize_t screen_tune_set(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	ssize_t ret = strnlen(buf, PAGE_SIZE);
	struct dsi_panel *panel;
	int input_param[4];

	panel = dev_get_drvdata(dev);
	if (panel == NULL) {
		pr_err("panel is NULL\n");
		return -EINVAL;
	}

	if (!dsi_panel_initialized(panel)) {
		pr_err("Panel off state. Ignore screen_mode set cmd\n");
		return -EINVAL;
	}

	sscanf(buf, "%d %d %d", &input_param[0], &input_param[1], &input_param[2]);

	panel->lge.sc_sat_step		= abs(input_param[0]);
	panel->lge.sc_hue_step		= abs(input_param[1]);
	panel->lge.sc_sha_step		= abs(input_param[2]);

	pr_info("sat : %d , hue = %d , sha = %d\n",
			panel->lge.sc_sat_step, panel->lge.sc_hue_step,
			panel->lge.sc_sha_step);

	if (panel->lge.ddic_ops && panel->lge.ddic_ops->lge_set_screen_tune)
		panel->lge.ddic_ops->lge_set_screen_tune(panel);

	panel->lge.sharpness_status = 0x01;

	if (panel->lge.ddic_ops && panel->lge.ddic_ops->lge_display_control_store)
		panel->lge.ddic_ops->lge_display_control_store(panel, true);

	return ret;
}
static DEVICE_ATTR(screen_tune, S_IRUGO | S_IWUSR | S_IWGRP,
					screen_tune_get, screen_tune_set);

static ssize_t rgb_tune_get(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct dsi_panel *panel;

	panel = dev_get_drvdata(dev);
	if (!panel) {
		pr_err("panel is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d %d %d %d \n", panel->lge.cm_preset_step,
					panel->lge.cm_red_step,
					panel->lge.cm_green_step,
					panel->lge.cm_blue_step);
}

static ssize_t rgb_tune_set(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	ssize_t ret = strnlen(buf, PAGE_SIZE);
	struct dsi_panel *panel;
	int input_param[4];

	panel = dev_get_drvdata(dev);
	if (panel == NULL) {
		pr_err("panel is NULL\n");
		return -EINVAL;
	}

	if (!dsi_panel_initialized(panel)) {
		pr_err("Panel off state. Ignore screen_mode set cmd\n");
		return -EINVAL;
	}

	sscanf(buf, "%d %d %d %d", &input_param[0], &input_param[1], &input_param[2], &input_param[3]);

	panel->lge.cm_preset_step = input_param[0];
	panel->lge.cm_red_step    = abs(input_param[1]);
	panel->lge.cm_green_step  = abs(input_param[2]);
	panel->lge.cm_blue_step   = abs(input_param[3]);

	if(panel->lge.cm_preset_step > 4)
		panel->lge.cm_preset_step = 4;

	pr_info("preset : %d , red = %d , green = %d , blue = %d \n",
			panel->lge.cm_preset_step, panel->lge.cm_red_step,
			panel->lge.cm_green_step, panel->lge.cm_blue_step);

	if (panel->lge.cm_preset_step == 2 &&
			!(panel->lge.cm_red_step | panel->lge.cm_green_step | panel->lge.cm_blue_step)) {
		panel->lge.dgc_status = 0x00;
	} else {
		if (panel->lge.ddic_ops && panel->lge.ddic_ops->lge_set_custom_rgb)
			panel->lge.ddic_ops->lge_set_custom_rgb(panel, true);
		panel->lge.dgc_status = 0x01;
	}

	if (panel->lge.ddic_ops && panel->lge.ddic_ops->lge_display_control_store)
		panel->lge.ddic_ops->lge_display_control_store(panel, true);

	return ret;
}
static DEVICE_ATTR(rgb_tune, S_IRUGO | S_IWUSR | S_IWGRP,
					rgb_tune_get, rgb_tune_set);

int lge_color_manager_create_sysfs(struct dsi_panel *panel, struct device *panel_sysfs_dev)
{
	int rc = 0;
	if ((rc = device_create_file(panel_sysfs_dev, &dev_attr_sharpness)) < 0)
		pr_err("add sharpness set node fail!");
	if ((rc = device_create_file(panel_sysfs_dev, &dev_attr_screen_mode)) < 0)
		pr_err("add screen_mode set node fail!");
	if ((rc = device_create_file(panel_sysfs_dev, &dev_attr_screen_tune)) < 0)
		pr_err("add screen_tune set node fail!");
	if ((rc = device_create_file(panel_sysfs_dev, &dev_attr_rgb_tune)) < 0)
		pr_err("add rgb_tune set node fail!");
	return rc;
}
