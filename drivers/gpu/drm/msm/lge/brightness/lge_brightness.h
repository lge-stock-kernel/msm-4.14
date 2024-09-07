#ifndef _LGE_BRIGHTNESS_H_
#define _LGE_BRIGHTNESS_H_

extern int lge_blmap_br_to_bl(struct dsi_display *display, int brightness);
extern int lge_backlight_device_update_status(struct backlight_device *bd);

#endif // _LGE_BRIGHTNESS_H_

