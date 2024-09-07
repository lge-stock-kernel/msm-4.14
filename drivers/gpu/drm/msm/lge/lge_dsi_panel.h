#ifndef _H_LGE_DSI_PANEL_
#define _H_LGE_DSI_PANEL_

void lge_set_blank_called(void);
u32 lge_get_brightness(u32 bl_lvl);

extern int lge_dsi_panel_drv_init(struct dsi_panel *panel);
extern int lge_dsi_panel_drv_deinit(struct dsi_panel *panel);
extern int lge_dsi_panel_get(struct dsi_panel *panel, struct device_node *of_node);
extern void lge_dsi_panel_put(struct dsi_panel *panel);
extern bool lge_dsi_panel_is_power_off(struct dsi_panel *panel);
extern bool lge_dsi_panel_is_power_on_interactive(struct dsi_panel *panel);
extern bool lge_dsi_panel_is_power_on(struct dsi_panel *panel);
extern bool lge_dsi_panel_is_power_on_lp(struct dsi_panel *panel);
extern bool lge_dsi_panel_is_power_on_ulp(struct dsi_panel *panel);
#endif //_H_LGE_DSI_PANEL_
