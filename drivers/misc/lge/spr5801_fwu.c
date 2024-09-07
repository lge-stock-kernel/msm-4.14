/* Copyright (c) 2019, LGE Inc. All rights reserved.
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

#include <linux/crc16.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/sysfs.h>
#include "spr5801_ctrl.h"
#include "spr5801_util.h"
#include "spr5801_fwu.h"

#define STATUS_READ_RETRY 3
#define STATUS_MASK_ERR 0x2 
#define STATUS_MASK_DONE 0x1

#define STATUS_ERR_NO_ERR 0x01
#define STATUS_ERR_NA     0x00
#define STATUS_ERR_CRC    0x03
#define STATUS_ERR_UNKNOWN 0xFF

int gti_firmware_prepare(struct gti_device *gdev)
{
	int prog_state ;
	gdev->fwstat = FW_NO_INIT ;
	pr_err("%s: start.\n", __func__);
	prog_state = gpio_get_value(gdev->prog) ;
	if(!prog_state){
		gpio_set_value(gdev->prog, 1) ;
		gpio_set_value(gdev->reset, 0) ;
		mdelay(2) ;
		gpio_set_value(gdev->reset, 1) ;
		mdelay(5) ;
	}
	gdev->fwstat = FW_PREPARE ;

	return 0;
}

static int gti_validate_firmware_update(struct gti_device *dev)
{
	int retry = STATUS_READ_RETRY;
	char cmd[4] = {0x0, 0x0, 0x0, 0x0};
	int dload_stat  = 0;
	int ret;

	do {
		msleep(1);
		ret = gti_get_status(dev,&dload_stat);
		if((dload_stat == STATUS_ERR_UNKNOWN) || (dload_stat == STATUS_ERR_NA))
			continue ;
		else if(ret > 0 && ((dload_stat & STATUS_MASK_DONE) || (dload_stat & STATUS_MASK_ERR)))
			break ;
	} while (retry--);

	if(dload_stat == STATUS_ERR_CRC){
		pr_err("%s: crc err retry start \n", __func__);

		cmd[0] = CMD_FWEND;
		ret = gti_i2c_send(dev, cmd, 4);
		retry = STATUS_READ_RETRY;
		do {
			msleep(1);
			ret = gti_get_status(dev,&dload_stat);
			if((dload_stat == STATUS_ERR_UNKNOWN) || (dload_stat == STATUS_ERR_NA))
				continue ;
			else if(ret > 0 && ((dload_stat & STATUS_MASK_DONE) || (dload_stat & STATUS_MASK_ERR)))
				break ;
		} while (retry--);
		pr_err("%s: crc err retry result %d \n", __func__,dload_stat );

	}
	else if((dload_stat == STATUS_ERR_UNKNOWN) || (dload_stat == STATUS_ERR_NA)){
		pr_err("%s: unknown err retry start \n", __func__ );
		gpio_set_value(dev->reset, 0) ;
		mdelay(2) ;
		gpio_set_value(dev->reset, 1) ;
		mdelay(5) ;
		cmd[0] = CMD_FWEND;
		ret = gti_i2c_send(dev, cmd, 4);
		retry = STATUS_READ_RETRY;
		do {
			msleep(1);
			ret = gti_get_status(dev,&dload_stat);
			if((dload_stat == STATUS_ERR_UNKNOWN) || (dload_stat == STATUS_ERR_NA))
				continue ;
			else if(ret > 0 && ((dload_stat & STATUS_MASK_DONE) || (dload_stat & STATUS_MASK_ERR)))
				break ;
		} while (retry--);
		pr_err("%s: unknown err retry result %d \n", __func__,dload_stat );
	}

	if ((ret < 0) && (dload_stat != STATUS_ERR_NO_ERR)) {
		pr_err("%s: i2c error on status\n", __func__);
		return ret;
	}

	if ((retry < 0) && (dload_stat != STATUS_ERR_NO_ERR)) {
		pr_err("%s: timed out 0x%02x \n", __func__,dload_stat );
		return -ETIMEDOUT;
	}

	return (dload_stat & STATUS_MASK_ERR) ? -EINVAL: 0;
}

#if 0
static int gti_firmware_update(struct gti_device *dev,
		const u8 *data, size_t size)
{
	size_t left = size, len;
	char cmd[4] = {0x0, 0x0, 0x0, 0x0};
	int ret;

	if (dev->fwstat != FW_PREPARE)
		return -ENODEV;

	dev->fwstat = FW_DOWNLOAD;
	pr_err("%s: start \n", __func__);

	do {

		if (left > size)
			len = size;
		else
			len = left;

		memcpy(dev->fw_buf, data, len);
		pr_err("%s: send download cmd \n", __func__);
		cmd[0] = CMD_FWDN;
		ret = gti_i2c_send(dev, cmd, 4);
		if (ret < 0) {
			pr_err("%s: i2c error on command fw download\n",
					__func__);
			break;
		}
		mdelay(2);
		pr_err("%s: send download dat \n", __func__);
		ret = gti_i2c_send(dev, dev->fw_buf, len );
		if (ret < 0) {
			pr_err("%s: i2c error on fw download(%u/%u)\n",
					__func__, left, size);
			break;
		}
		mdelay(2);
		ret = gti_validate_firmware_update(dev);
		if (ret){
			pr_err("%s: validate_firmware_update failed \n", __func__);
			break;
		}

		if(ret == 0)
		{
			data += len;
			left -= len;
			pr_err("GTI FW Wrote [%u/%u]\n", (size-left), size);
		}
	} while (left);

	cmd[0] = CMD_FWEND;
	ret = gti_i2c_send(dev, cmd, 4);
	if (ret < 0) {
		pr_err("%s: i2c error on command fw downlaod end\n", __func__);
		goto out;
	}

	if (left) {
		pr_err("%s: FW DOWNLOAD FAILED \n", __func__);
		ret = -EIO;
		goto out;
	}

	pr_err("%s: FW DOWNLOAD SUCCESS!! \n", __func__);
	ret = 0;
out:
	dev->fwstat = FW_DONE;
	return ret;
}
#else
static int gti_firmware_update(struct gti_device *dev,
		const u8 *data, size_t size)
{
	char start_cmd[4] = {CMD_FWDN, 0x0, 0x0, 0x0};
	char end_cmd[4] = {CMD_FWEND, 0x0, 0x0, 0x0};
	struct i2c_msg i2c_msg[2];
	int ret;
	int read_fwv ;

	if (dev->fwstat != FW_PREPARE)
		return -ENODEV;

	dev->fwstat = FW_DOWNLOAD;
	pr_err("%s: start \n", __func__);

	memcpy(dev->fw_buf, data, size);
	pr_err("%s: send download cmd \n", __func__);

	ret = gti_i2c_send(dev, start_cmd, 4);
	if (ret < 0) {
		pr_err("%s: i2c error on command fw download\n",__func__);
	}
	mdelay(2);
	pr_err("%s: send download dat \n", __func__);

	i2c_msg[0].addr = dev->client->addr;
	i2c_msg[0].flags = 0;
	i2c_msg[0].len = size;
	i2c_msg[0].buf = (char *) dev->fw_buf;

	i2c_msg[1].addr = dev->client->addr;
	i2c_msg[1].flags = 0;
	i2c_msg[1].len = 4;
	i2c_msg[1].buf = (char *) end_cmd;

	ret = gti_i2c_transfer(dev, i2c_msg, 2);
	if (ret < 0 ) {
		pr_err("%s: FW DOWNLOAD FAILED \n", __func__);
		ret = -EIO;
		goto out;
	}
	pr_err("%s: GTI FW Wrote [%u]\n", __func__, size);

	gpio_set_value(dev->reset, 0) ;
	mdelay(2) ;
	gpio_set_value(dev->reset, 1) ;
	mdelay(5) ;

	mdelay(25); // boot wait
	ret = gti_validate_firmware_update(dev);
	if (ret){
		pr_err("%s: validate_firmware_update failed \n", __func__);
	}

	if (ret) {
		pr_err("%s: FW DOWNLOAD FAILED \n", __func__);
		ret = -EIO;
		goto out;
	}

	gti_get_fw_version(dev, &read_fwv);
	if(read_fwv != dev->dlver){
		pr_err("%s: firmware read %d download %d \n", __func__, read_fwv, dev->dlver);
	}
	pr_err("%s: FW DOWNLOAD SUCCESS!! \n", __func__);
	ret = 0;
out:
	dev->fwstat = FW_DONE;
	return ret;
}
#endif

int gti_update_fw(struct gti_device *gdev)
{
	const struct firmware *fw = NULL;
	int ret;
	u16 magic ;

	pr_err("%s: start \n", __func__);
	/* load firmware from file */
	ret = request_firmware(&fw, gdev->fw_name,
			&gdev->client->dev);
	if (ret) {
		pr_err("%s: cannot read firmware\n", __func__);
		release_firmware(fw);
		return ret;
	};
	gti_firmware_prepare(gdev);
	pr_err("%s: firmware update \n", gdev->fw_name);

	magic = (u16)(fw->data[0] << 8) | (fw->data[1]) ;
	if(magic == 0x5801)
		ret = gti_firmware_update(gdev, fw->data + 4, fw->size -4);
	else
		ret = gti_firmware_update(gdev, fw->data, fw->size );

	if (ret) {
		pr_err("%s: cannot update firmware\n", __func__);
		release_firmware(fw);
		return ret;
	};

	/* close firmware */
	release_firmware(fw);

	return ret ;
}

int gti_fw_exist(struct i2c_client *client)
{
	struct gti_device *gdev = i2c_get_clientdata(client);
	struct device *dev = &client->dev;
	const struct firmware *fw = NULL;
	int ret ;
	u16 magic ;
	u16 crc ;

	/* load firmware from file */
	pr_err("%s: request firmware \n", __func__);
	ret = request_firmware(&fw, gdev->fw_name,
			dev);
	if (ret) {
		release_firmware(fw);
		pr_err("%s: cannot read firmware\n", __func__);
		return ret;
	};

	magic = (u16)(fw->data[0] << 8) | (fw->data[1]) ;
	if(magic == 0x5801){
		gdev->dlver = fw->data[2] ;
		if(fw->data[3] == 1){
			crc = (u16)(fw->data[4] << 8) | (fw->data[5]) ;
			pr_err("%s: magic 0x%04x ver %d with crc 0x%04x \n", __func__, magic, gdev->dlver, crc );
		}
		else {
			pr_err("%s: magic 0x%04x ver %d without crc \n", __func__, magic, gdev->dlver );
		}
	}
	else {
		gdev->dlver = SPR5801_FW_VER ;
	}

	pr_err("%s: firmware %s size %d ver %d\n", __func__, gdev->fw_name, fw->size, gdev->dlver);
	/* close firmware */
	release_firmware(fw);
	return 0 ;
}

