/* Copyright (c) 2016 LG Electronics, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/delay.h>

#define DEBUG 0

/* predefined cmd */
#define CMD_MME_POWER_ON                30
#define CMD_MME_POWER_OFF               31
#define CMD_MME_NFC_ON                  40
#define CMD_MME_NFC_OFF                 41

static struct class *mme_class;
static struct device *mme_dev;
static int mme_major;

static int nfc_state = 0;
static int wmc_state = 0;

/* global variables should be updated from dts */
static int gpio_sleep_n = -1;  // DRV8838_SLEEP_N_Pin

#ifdef CONFIG_MACH_SM6150_MH3
#define ISP7022
#endif

#ifdef ISP7022
static int gpio_vcc_en = -1;  // DRV8838_FAULT_N_Pin
#endif

static unsigned int command_type = 0; // send_commnad (odd: once, even: repeat)
/*
 * [mme_command] node read/write function
 */
static ssize_t lge_show_mme_command (struct device *dev,
        struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", command_type);
}

static void lge_wmc_on(void){
#ifdef ISP7022
	// Set WMC vcc to high
	gpio_set_value(gpio_vcc_en, 1);
	mdelay(1);
#endif
	// Set WMC n_sleep to high
	gpio_set_value(gpio_sleep_n, 1);
        wmc_state = 1;
}
static void lge_wmc_off(void){
	// Set WMC n_sleep to low
	gpio_set_value(gpio_sleep_n, 0);
#ifdef ISP7022
	// Set WMC vcc to low
	mdelay(1);
	gpio_set_value(gpio_vcc_en, 0);
#endif
	wmc_state = 0;
}


static ssize_t lge_store_mme_command (struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
    sscanf(buf, "%d", &command_type);
    pr_err("[MME] %s: Start to send. command_type=%d\n", __func__, command_type);

    switch(command_type) {
        case CMD_MME_POWER_ON:
            pr_err("[MME] CMD_MME_POWER_ON\n");
            if (nfc_state == 1)
            {
                pr_err("[MME] NFC is active\n");
                break;
            }
            if (wmc_state == 1)
            {
                pr_err("[MME] WMC is already on\n");
                break;
            }
            lge_wmc_on();
            break;

        case CMD_MME_POWER_OFF:
            pr_err("[MME] CMD_MME_POWER_OFF\n");

            if (nfc_state == 1)
            {
                pr_err("[MME] NFC is active\n");
                break;
            }
            if (wmc_state == 0)
            {
                pr_err("[MME] WMC is already off\n");
                break;
            }
            lge_wmc_off();
            break;

        case CMD_MME_NFC_ON:
            pr_err("[MME] CMD_MME_NFC_ON\n");

            lge_wmc_off();

            // Set NFC state to active
            nfc_state = 1;
            break;

        case CMD_MME_NFC_OFF:
            pr_err("[MME] CMD_MME_NFC_OFF\n");

            // Set NFC state to deactive
            nfc_state = 0;
            break;

        default:
            pr_err("[MME] Not suppported cmd_id(%d)\n", command_type);
            return -EINVAL;
    }

    return count;
}

/*        sysfs       name    perm     cat function         echo function   */
static DEVICE_ATTR(mme_command, 0664, lge_show_mme_command, lge_store_mme_command);

static struct attribute *lge_mme_attrs[] = {
    &dev_attr_mme_command.attr,
    NULL
};

static const struct attribute_group lge_mme_files = {
    .attrs  = lge_mme_attrs,
};

static int mme_gpio_init(struct device *dev)
{
    int ret = 0;

#ifdef ISP7022
    gpio_vcc_en = of_get_named_gpio(dev->of_node, "lge-mme,gpio_vcc_en", 0);
    pr_err("[MME] %s: gpio_vcc_en=%d\n",__func__, gpio_vcc_en);
    /* request gpio and set gpio direction*/
    ret = gpio_request_one(gpio_vcc_en, GPIOF_OUT_INIT_LOW, "lge-mme,gpio_vcc_en");
    pr_err("[MME] gpio_request_one: gpio_vcc_en GPIO=%d  ret= %d \n", gpio_vcc_en,ret);
#endif

    gpio_sleep_n = of_get_named_gpio(dev->of_node, "lge-mme,gpio_sleep_n", 0);
    pr_err("[MME] %s: gpio_sleep_n=%d\n",__func__, gpio_sleep_n);
    /* request gpio and set gpio direction*/
    ret = gpio_request_one(gpio_sleep_n, GPIOF_OUT_INIT_LOW, "lge-mme,gpio_sleep_n");
    pr_err("[MME] gpio_request_one: nSleep GPIO=%d ret= %d \n", gpio_sleep_n,ret);

    return ret;
}

static int mme_gpio_remove()
{
    lge_wmc_off();
#ifdef ISP7022
    if (gpio_vcc_en != -1)
        gpio_free(gpio_vcc_en);
    pr_err("[MME] gpio_free: gpio_vcc_en GPIO=%d\n", gpio_vcc_en);
#endif
    if (gpio_sleep_n != -1)
        gpio_free(gpio_sleep_n);
    pr_err("[MME] gpio_free: nSleep GPIO=%d\n", gpio_sleep_n);

    return 0;
}

extern int lge_get_wmc_support(void);

static int __init lge_mme_probe(struct platform_device *pdev)
{
    int ret = 0;

    if (lge_get_wmc_support() == 0) {
        pr_err("[MME] not support wmc %d\n", lge_get_wmc_support());
        return 0;
    }

    pr_err("[MME] %s: probe enter\n", __func__);

    /* TO BE : mme drv gpio init */
    ret = mme_gpio_init(&pdev->dev);
    if (ret < 0 ) {
        pr_err("[MME] %s: gpio init failed, ret %d\n", __func__, ret);
        return ret;
    }

    mme_class = class_create(THIS_MODULE, "lge_mme");
    if (IS_ERR(mme_class)) {
        pr_err("[MME] %s: class_create() failed ENOMEM\n", __func__);
        ret = -ENOMEM;
        goto exit;
    }

    mme_dev = device_create(mme_class, NULL, MKDEV(mme_major, 0), NULL, "mme_ctrl");
    if (IS_ERR(mme_dev)) {
        pr_err("[MME] %s: device_create() failed\n", __func__);
        ret = PTR_ERR(mme_dev);
        goto exit;
    }

    /* create /sys/class/lge_mme/mme_ctrl/mme_command */
    ret = device_create_file(mme_dev, &dev_attr_mme_command);
    if (ret < 0) {
        pr_err("[MME] %s: device create file fail\n", __func__);
        goto exit;
    }

    pr_info("[MME] %s: probe done\n", __func__);
    return 0;

exit:
    pr_err("[MME] %s: probe fail - %d\n", __func__, ret);
    return ret;
}

static int lge_mme_remove(struct platform_device *pdev)
{
    device_remove_file(mme_dev, &dev_attr_mme_command);
    device_destroy(mme_class, MKDEV(mme_major, 0));
    class_destroy(mme_class);

    mme_gpio_remove();
    pr_info("[MME] %s: remove done\n", __func__);

    return 0;
}

/* device driver structures */
static struct of_device_id mme_match_table[] = {
    { .compatible = "lge-mme",},
    {},
};

static struct platform_driver lge_mme_driver __refdata = {
    .probe = lge_mme_probe,
    .remove = lge_mme_remove,
    .driver = {
        .name = "lge_mme",
        .owner = THIS_MODULE,
        .of_match_table = mme_match_table,
    },
};

/* driver init funcion */
static int __init lge_mme_init(void)
{
    int ret = 0;

    ret = platform_driver_register(&lge_mme_driver);
    if (ret < 0)
        pr_err("[MME] %s : platform_driver_register() err=%d\n", __func__, ret);

    return ret;
}

/* driver exit function */
static void __exit lge_mme_exit(void)
{
    platform_driver_unregister(&lge_mme_driver);
}

module_init(lge_mme_init);
module_exit(lge_mme_exit);

MODULE_DESCRIPTION("LGE MME driver for LG Pay");
MODULE_AUTHOR("jinsol.jo <jinsol.jo@lge.com>");
MODULE_LICENSE("GPL");
