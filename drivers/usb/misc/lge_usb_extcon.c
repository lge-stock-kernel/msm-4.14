/*
 * LGE_USB_EXTCON - LGE USB extcon driver
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

#include <linux/extcon.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>

struct lge_usb_extcon {
	struct device			*dev;
	struct extcon_dev		*edev;

	bool				onoff;
	struct delayed_work		host_onoff_work;
};

static struct lge_usb_extcon *__lge_usb_extcon = NULL;

static const unsigned int usb_extcon_switch[] = {
	EXTCON_USB_HOST,
	EXTCON_NONE,
};

static void lge_usb_extcon_onoff_work(struct work_struct *work)
{
	struct lge_usb_extcon *pdev = container_of(work, struct lge_usb_extcon,
						host_onoff_work.work);
	static bool curr_host_mode = false;

	if(!pdev)
		return;

	if (curr_host_mode == pdev->onoff)
		return;

	dev_info(pdev->dev, "%s : %s\n", __func__, pdev->onoff ? "on" : "off");
	extcon_set_state_sync(pdev->edev, EXTCON_USB_HOST, pdev->onoff);
	curr_host_mode = pdev->onoff;

	return;
}

static ssize_t host_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct lge_usb_extcon *pdev = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, pdev->onoff ? "1" : "0");
}

static ssize_t host_mode_store(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct lge_usb_extcon *pdev = dev_get_drvdata(dev);

	if (sysfs_streq(buf, "1")) {
		pdev->onoff = true;
	} else {
		pdev->onoff = false;
	}
	schedule_delayed_work(&pdev->host_onoff_work, msecs_to_jiffies(0));

	return count;
}
static DEVICE_ATTR_RW(host_mode);

void lge_usb_extcon_start_host(bool on)
{
	struct lge_usb_extcon *pdev = __lge_usb_extcon;

	if(!pdev)
		return;

	pdev->onoff = on;
	schedule_delayed_work(&pdev->host_onoff_work, msecs_to_jiffies(0));

	return;
}
EXPORT_SYMBOL(lge_usb_extcon_start_host);

static int lge_usb_extcon_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct lge_usb_extcon *lge_usb_extcon;

	dev_info(&pdev->dev, "%s\n", __func__);

	lge_usb_extcon = devm_kzalloc(dev, sizeof(*lge_usb_extcon), GFP_KERNEL);
	if (!lge_usb_extcon) {
		dev_err(dev, "out of memory\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, lge_usb_extcon);
	lge_usb_extcon->dev = &pdev->dev;

	lge_usb_extcon->edev = devm_extcon_dev_allocate(dev, usb_extcon_switch);
	if (IS_ERR(lge_usb_extcon->edev)) {
		dev_err(dev, "failed to allocate extcon device\n");
		platform_set_drvdata(pdev, NULL);
		return -ENOSYS;
	}

	if (devm_extcon_dev_register(dev, lge_usb_extcon->edev) < 0) {
		dev_err(dev, "failed to register extcon device\n");
		platform_set_drvdata(pdev, NULL);
		return -EINVAL;
	}

	__lge_usb_extcon = lge_usb_extcon;

	device_create_file(&pdev->dev, &dev_attr_host_mode);

	INIT_DELAYED_WORK(&lge_usb_extcon->host_onoff_work, lge_usb_extcon_onoff_work);

	return 0;
}

static const struct of_device_id lge_usb_extcon_match_table[] = {
	{ .compatible = "lge,lge_usb_extcon" },
	{ }
};
MODULE_DEVICE_TABLE(of, lge_sbu_switch_match_table);

static struct platform_driver lge_usb_extcon_driver = {
	.driver = {
		.name = "lge_usb_extcon",
		.of_match_table = lge_usb_extcon_match_table,
	},
	.probe = lge_usb_extcon_probe,
};
module_platform_driver(lge_usb_extcon_driver);

MODULE_DESCRIPTION("LGE USB extcon driver");
MODULE_LICENSE("GPL v2");
