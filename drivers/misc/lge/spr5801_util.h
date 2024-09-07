/* Copyright (c) 2019, LGE Inc. All rights reserved.
 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
*/
#ifndef SPR5801_UTIL_H
#define SPR5801_UTIL_H

#define CMD_DEBUG  0x00
#define CMD_DEVID  0x01
#define CMD_FWVER  0x02
#define CMD_STATUS 0x03
#define CMD_FWDN   0x04
#define CMD_MEMR   0x05
#define CMD_MEMW   0x06
#define CMD_FWEND  0x08

int gti_i2c_send(struct gti_device *dev, const char *buf, int count);
int gti_i2c_recv(struct gti_device *dev, char *buf, int count);
int gti_parse_dt(struct i2c_client *client);
int gti_get_status(struct gti_device *dev, int *chip_stat);
int gti_get_fw_version(struct gti_device *dev, int *fw_ver);
int gti_get_device_id(struct gti_device *dev, int *vid, int *pid);
int gti_sysfs_create(struct i2c_client *client);
int gti_sysfs_remove(struct i2c_client *client);
int gti_clr_mark(struct gti_device *dev);
int gti_i2c_transfer(struct gti_device *dev, struct i2c_msg *i2c_msg, int num_msg);
#endif
