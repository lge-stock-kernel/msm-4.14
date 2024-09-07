/*
 *  Copyright (c) 2018 LG Electronic
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/sysfs.h>
#include <linux/of_gpio.h>
#include <linux/stat.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/dma-mapping.h>
#include <linux/regulator/consumer.h>
#include <soc/qcom/lge/board_lge.h>
#include "spr5801_ctrl.h"
#include "spr5801_util.h"
#include "spr5801_fwu.h"

#define MODULE_NAME "gti_ctrl"
#define DO_NOT_1V8_POWER_DOWN 1
#define RESET_TIME 5

#ifdef CONFIG_LGE_USB_EXTCON
extern void lge_usb_extcon_start_host(bool on);
#endif

int gti_power_on(struct gti_device *gdev)
{
	int ret ;
#ifdef CONFIG_LGE_USB_EXTCON
	lge_usb_extcon_start_host(true);
#endif
	pr_err("%s: power on start \n", __func__);

	if(!regulator_is_enabled(gdev->smps_1v8)){
		ret = regulator_set_voltage(gdev->smps_1v8, 2010000, 2010000 ) ;
		if(ret < 0) pr_err("%s: smps_1v8 set volt fail \n", __func__);
		ret = regulator_set_load(gdev->smps_1v8, 50000);
		if(ret < 0)	pr_err("%s: smps_1v8 set load fail\n", __func__);
		ret = regulator_enable(gdev->smps_1v8) ;
		if(ret < 0)	pr_err("%s: smps_1v8 enable fail\n", __func__);
	}

	if(!regulator_is_enabled(gdev->smps_0v9)){
		ret = regulator_set_voltage(gdev->smps_0v9, 1130000, 1130000 ) ;
		if(ret < 0) pr_err("%s: smps_0v9 set volt fail \n", __func__);
		ret = regulator_set_load(gdev->smps_0v9, 700000);
		if(ret < 0)	pr_err("%s: smps_0v9 set load  fail\n", __func__);
		ret = regulator_enable(gdev->smps_0v9) ;
		if(ret < 0)	pr_err("%s: smps_0v9 enable fail\n", __func__);
	}

	gpio_set_value(gdev->ldo_3v3, 1) ;
	mdelay(1) ;
	gpio_set_value(gdev->ldo_1v8, 1) ;
	gpio_set_value(gdev->ldo_0v9, 1) ;
	mdelay(2) ;

	gpio_set_value(gdev->reset, 1) ;
	if(gdev->hw_rev == 0)
		gpio_set_value(gdev->prog, 1) ;
	else
		gpio_set_value(gdev->prog, 0) ;

	gpio_set_value(gdev->reset, 0) ;
	mdelay(RESET_TIME) ;
	gpio_set_value(gdev->reset, 1) ;
	mdelay(RESET_TIME) ;
	gpio_set_value(gdev->i2c_en, 1) ;
	return 0 ;
}

int gti_power_off(struct gti_device *gdev)
{
	int ret ;
	pr_err("%s: power off start \n", __func__);
	gpio_set_value(gdev->prog, 1) ;
	gpio_set_value(gdev->i2c_en, 0) ;
	if(gdev->i2c_hiz){
		gpio_set_value(gdev->ldo_0v9, 0) ;
		gpio_set_value(gdev->ldo_1v8, 0) ;
		gpio_set_value(gdev->ldo_3v3, 0) ;
		gpio_set_value(gdev->prog, 0) ;
		gpio_set_value(gdev->reset, 0) ;
	}
	else {
		gpio_set_value(gdev->ldo_0v9, 0) ; // i2c and gpio pin is pseudo open drain
		gpio_set_value(gdev->ldo_1v8, 0) ;
		gpio_set_value(gdev->ldo_3v3, 0) ;
		gpio_set_value(gdev->ldo_3v3, 1) ;
		mdelay(1) ;
		gpio_set_value(gdev->ldo_1v8, 1) ;
		gpio_set_value(gdev->ldo_0v9, 1) ;
		mdelay(2) ;
	}

#ifdef CONFIG_LGE_USB_EXTCON
	lge_usb_extcon_start_host(false);
#endif

	if(regulator_is_enabled(gdev->smps_1v8)){
		ret = regulator_disable(gdev->smps_1v8) ;
		if(ret < 0)	pr_err("%s: smps_1v8 disable fail\n", __func__);

		ret = regulator_set_load(gdev->smps_1v8, 0);
		if(ret < 0)	pr_err("%s: smps_1v8 clr load fail\n", __func__);
	}

	if(regulator_is_enabled(gdev->smps_0v9)){
		ret = regulator_disable(gdev->smps_0v9) ;
		if(ret < 0)	pr_err("%s: smps_0v9 disable fail\n", __func__);

		ret = regulator_set_load(gdev->smps_0v9, 0);
		if(ret < 0)	pr_err("%s: smps_0v9 clr load fail\n", __func__);
	}

	return 0 ;
}

int gti_init(struct i2c_client *client)
{
	struct gti_device *gdev = i2c_get_clientdata(client);
	int ret = -1;
	int read_vid, read_pid ;
	int retry = 3 ;

	gti_power_on(gdev) ;

	while(1){
		ret = gti_get_device_id(gdev, &read_vid, &read_pid) ;
		if(ret == 0 )
			break ;
		else {
			retry--;
			if(retry == 0)
				return ret ;
			else
				mdelay(1) ;
		}
	}

	gdev->vid = read_vid;
	gdev->pid = read_pid;
	if(read_vid == SPR5801_VID && (read_pid & 0xffff0000) == SPR5801_PID){
		pr_err("%s: device is spr5801 \n", __func__);
	}
	else {
		pr_err("%s: device vid 0x%4x pid 0x%4x \n", __func__,read_vid, read_pid );
	}

	gdev->stat = INIT;
	return 0 ;
}

int gti_chk_ver(struct i2c_client *client)
{
	struct gti_device *gdev = i2c_get_clientdata(client);
	int ret = -1;
	int read_fwv ;
	int need_update =0;

	ret = gti_get_fw_version(gdev, &read_fwv);
	if(ret < 0)
		return ret ;

	gdev->fwver = read_fwv ;
	if(read_fwv < SPR5801_FW_VER)
		need_update = 1 ;

	pr_err("%s: fw version is %d \n", __func__, read_fwv);
	gdev->stat = CHECK;
	return need_update ;
}

static int gti_ctrl_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct gti_device *gdev;
	struct device *dev = &client->dev;
	int ret = -1;
	int update = 0 ;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("%s: i2c functionality not supported\n", __func__);
		return -EIO;
	}

	gdev = devm_kzalloc(&client->dev, sizeof(struct gti_device),
			GFP_KERNEL);
	if (!gdev) {
		pr_err("%s: no memory\n", __func__);
		return -ENOMEM;
	}

	gdev->stat = PWR_OFF;
	gdev->fwstat = FW_NO_INIT;
	gdev->client = client;
	i2c_set_clientdata(client, gdev);

	gdev->fw_buf = devm_kzalloc(&client->dev,DMA_BUF_SIZE, GFP_KERNEL | GFP_DMA);
	if(gdev->fw_buf == NULL){
		pr_err("%s: dma_alloc failed \n", __func__);
		goto err_gti;
	}

	gdev->smps_1v8 = devm_regulator_get(dev, "gti,smps-1v8") ;
	if(gdev->smps_1v8 == NULL){
		pr_err("%s: 1v8 devm_regulator_get failed\n", __func__);
	}
	gdev->smps_0v9 = devm_regulator_get(dev, "gti,smps-0v9") ;
	if(gdev->smps_0v9 == NULL){
		pr_err("%s: 0v9 devm_regulator_get failed\n", __func__);
	}

	pr_err("%s: start \n", __func__);
	ret = gti_parse_dt(client);
	if(ret){
		pr_err("%s: gti_parse_dt err\n", __func__);
		goto err_gti;
	}
	ret = gti_fw_exist(client);
	if(ret)
		pr_err("%s: fw not found \n", __func__);

	ret = gti_init(client);
	if(ret){
		pr_err("%s: gti_init err\n", __func__);
		goto err_gti ;
	}

	update = gti_chk_ver(client);
	init_completion(&gdev->complete);

	gdev->stat = READY;
	ret = gti_sysfs_create(client) ;
	if (ret) {
		pr_err("%s: cannot create sysfs\n", __func__);
		goto err_sysfs_create;
	}

	if(gdev->i2c_hiz)
		gti_power_off(gdev) ;

	return 0;

err_sysfs_create:
err_gti:
	if(gdev->i2c_hiz)
		gti_power_off(gdev) ;

	devm_kfree(&client->dev, gdev->fw_buf);
	devm_kfree(&client->dev, gdev);
	i2c_set_clientdata(client, NULL);
	return ret;
}

static int gti_ctrl_remove(struct i2c_client *client)
{
	struct gti_device *gdev = i2c_get_clientdata(client);

	if(gdev->smps_1v8)
		regulator_put(gdev->smps_1v8);
	if(gdev->smps_0v9)
		regulator_put(gdev->smps_0v9);
	if(gdev->fw_buf){
		kfree(gdev->fw_buf) ;
	}
	gti_sysfs_remove(client);
	i2c_set_clientdata(client, NULL);

	return 0;
}

static struct of_device_id gti_match_table[] = {
	{ .compatible = "lge,gti_ctrl", },
	{ }
};
MODULE_DEVICE_TABLE(of, gti_match_table);

static const struct i2c_device_id gti_ctrl_id[] = {
	{ "gti_ctrl", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, gti_ctrl_id);

static struct i2c_driver gti_ctrl_driver = {
	.driver = {
		.name = MODULE_NAME,
		.owner = THIS_MODULE,
		.of_match_table = gti_match_table,
	},
	.probe = gti_ctrl_probe,
	.remove = gti_ctrl_remove,
	.id_table = gti_ctrl_id,
};

static int __init gti_ctrl_init(void)
{
	enum lge_sku_carrier_type  model_sku ;

	model_sku = lge_get_sku_carrier() ;
	if(model_sku == HW_SKU_KR)
		i2c_add_driver(&gti_ctrl_driver);

	return 0 ;
}

static void __exit gti_ctrl_exit(void)
{
	enum lge_sku_carrier_type  model_sku ;

	model_sku = lge_get_sku_carrier() ;
	if(model_sku == HW_SKU_KR)
		i2c_del_driver(&gti_ctrl_driver);
}

module_init(gti_ctrl_init);
module_exit(gti_ctrl_exit);

MODULE_DESCRIPTION("gti control");
MODULE_AUTHOR("Dojip Kim <dojip.kim@lge.com>");
MODULE_LICENSE("GPL v2");
