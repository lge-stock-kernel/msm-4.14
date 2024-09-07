/* Copyright (c) 2012-2015, The Linux Foundation. All rights reserved.
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
#ifndef LGE_COLOR_MANAGER_H
#define LGE_COLOR_MANAGER_H

#include "dsi_panel.h"
#include "../lge_dsi_panel.h"

#define RGB_DEFAULT_PRESET	2
#define RGB_DEFAULT_RED		4
#define RGB_DEFAULT_BLUE	4
#define RGB_DEFAULT_GREEN	4

enum {
	RED      = 0,
	GREEN    = 1,
	BLUE     = 2,
	RGB_ALL  = 3
};

enum lge_gamma_correction_mode {
	LGE_GC_MOD_NOR = 0,
	LGE_GC_MOD_CIN,
	LGE_GC_MOD_SPO,
	LGE_GC_MOD_GAM,
	LGE_GC_MOD_MAX,
};

int lge_color_manager_create_sysfs(struct dsi_panel *panel, struct device *panel_sysfs_dev);
#endif /* LGE_COLOR_MANAGER_H */
