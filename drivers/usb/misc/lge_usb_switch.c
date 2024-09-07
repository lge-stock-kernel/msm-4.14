/*
 * LGE_USB_PATH_SWITCH - USB path switch driver
 *
 * Copyright (C) 2019 LG Electronics, Inc.
 * Author: sangmin978.lee@lge.com
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/of.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>

struct lge_usb_switch {
	struct device			*dev;

	/* GPIOs */
	struct gpio_desc		*usb_path_sel;
};

static ssize_t usb_switch_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct lge_usb_switch *pdev = dev_get_drvdata(dev);

	if (!IS_ERR( pdev->usb_path_sel))
		return snprintf(buf, PAGE_SIZE, gpiod_get_value(pdev->usb_path_sel) ? "usb1\n" : "usb0\n");

	return snprintf(buf, PAGE_SIZE, "none\n");
}

static ssize_t usb_switch_store(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct lge_usb_switch *pdev = dev_get_drvdata(dev);

	if (IS_ERR( pdev->usb_path_sel))
		return count;

	if (sysfs_streq(buf, "usb0"))
		gpiod_direction_output(pdev->usb_path_sel, 0);
	else if (sysfs_streq(buf, "usb1"))
		gpiod_direction_output(pdev->usb_path_sel, 1);

	return count;
}
static DEVICE_ATTR_RW(usb_switch);

static int lge_usb_switch_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct lge_usb_switch *lge_usb_switch;

	lge_usb_switch = devm_kzalloc(dev, sizeof(*lge_usb_switch), GFP_KERNEL);
	if (!lge_usb_switch) {
		dev_err(dev, "out of memory\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, lge_usb_switch);
	lge_usb_switch->dev = &pdev->dev;

	lge_usb_switch->usb_path_sel = devm_gpiod_get(dev, "lge,usb-path-sel", GPIOD_OUT_HIGH);
	if (IS_ERR(lge_usb_switch->usb_path_sel)) {
		dev_err(dev, "couldn't get usb_path_sel gpio\n");
		lge_usb_switch->usb_path_sel = NULL;
	}
	dev_info(dev, "usb path : %s\n", gpiod_get_value(lge_usb_switch->usb_path_sel) ? "usb1" : "usb0");

	device_create_file(&pdev->dev, &dev_attr_usb_switch);

	return 0;
}

static const struct of_device_id lge_usb_switch_match_table[] = {
	{ .compatible = "lge,lge_usb_switch" },
	{ }
};
MODULE_DEVICE_TABLE(of, lge_sbu_switch_match_table);

static struct platform_driver lge_usb_switch_driver = {
	.driver = {
		.name = "lge_usb_switch",
		.of_match_table = lge_usb_switch_match_table,
	},
	.probe = lge_usb_switch_probe,
};
module_platform_driver(lge_usb_switch_driver);

MODULE_DESCRIPTION("LGE USB Path Switch driver");
MODULE_LICENSE("GPL v2");
