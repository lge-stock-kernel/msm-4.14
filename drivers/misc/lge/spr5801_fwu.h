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

#ifndef SPR5801_FWU_H
#define SPR5801_FWU_H

enum {
	FW_NO_INIT,
	FW_PREPARE,
	FW_DOWNLOAD,
	FW_DONE,
	FW_UNKNOWN,
};

static const char *fwmode_str[] = {
	"FW NO INIT",
	"FW PREPARE",
	"FW DOWNLOAD",
	"FW DONE",
	"FW UNKNOWN",
};

int gti_firmware_prepare(struct gti_device *gdev);
void gti_update_fw_work(struct work_struct *work);
int gti_update_fw(struct gti_device *gdev);
int gti_fw_exist(struct i2c_client *client);
#endif