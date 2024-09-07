#ifndef _H_LGE_DSI_PANEL_DEF_
#define _H_LGE_DSI_PANEL_DEF_
#define NUM_COLOR_MODES  10
/**
 * enum lge_ddic_dsi_cmd_set_type = LGE DSI command set type
 * @
 */
enum lge_ddic_dsi_cmd_set_type {
	LGE_DDIC_DSI_DISP_CTRL_COMMAND_1 = 0,
	LGE_DDIC_DSI_DISP_CTRL_COMMAND_2,
	LGE_DDIC_DSI_DISP_DG_COMMAND_DUMMY,
	LGE_DDIC_DSI_DISP_CM_COMMAND_DUMMY,
	LGE_DDIC_DSI_DISP_CM_COMMAND_DEFAULT,
	LGE_DDIC_DSI_DISP_CTRL_DEEP_SLEEP_ENTER,
	LGE_DDIC_DSI_DISP_CTRL_DEEP_SLEEP_EXIT,
	LGE_DDIC_DSI_DISP_CTRL_HT_LCD_TUNE_0,
	LGE_DDIC_DSI_DISP_CTRL_HT_LCD_TUNE_1,
	LGE_DDIC_DSI_DISP_CTRL_HT_LCD_TUNE_2,
	LGE_DDIC_DSI_DISP_CTRL_HT_LCD_TUNE_3,
	LGE_DDIC_DSI_DISP_CTRL_HT_LCD_TUNE_4,
	LGE_DDIC_DSI_CMD_SET_MAX
};

struct lge_ddic_dsi_panel_cmd_set {
	enum lge_ddic_dsi_cmd_set_type type;
	enum dsi_cmd_set_state state;
	u32 count;
	u32 ctrl_idx;
	struct dsi_cmd_desc *cmds;
};

struct lge_panel_pin_seq {
	int gpio;
	u32 level;
	u32 sleep_ms;
};

struct lge_blmap {
	int size;
	int *map;
};

struct lge_gpio_entry {
	char name[32];
	int gpio;
};

struct dsi_panel;

struct lge_ddic_ops {
	int (*lge_dsi_panel_power_on)(struct dsi_panel *panel);
	int (*lge_dsi_panel_post_enable)(struct dsi_panel *panel);
	void (*lge_panel_enter_deep_sleep)(struct dsi_panel *panel);
	void (*lge_panel_exit_deep_sleep)(struct dsi_panel *panel);
	/* For DISPLAY_COLOR_MANAGER */
	void (*lge_display_control_store)(struct dsi_panel *panel, bool send_cmd);
	void (*lge_set_custom_rgb)(struct dsi_panel *panel, bool send_cmd);
	void (*lge_set_rgb_tune)(struct dsi_panel *panel, bool send_cmd);
	void (*lge_set_screen_tune)(struct dsi_panel *panel);
	void (*lge_set_screen_mode)(struct dsi_panel *panel, bool send_cmd);
	void (*lge_send_screen_mode_cmd)(struct dsi_panel *panel, int index);
	void (*sharpness_set)(struct dsi_panel *panel, int mode);
	/* for DISPLAY_HT_LCD_TUNE */
	void (*lge_set_ht_lcd_tune)(struct dsi_panel *panel, int index);
};
struct lge_dsi_panel {
	int *pins;
	int pins_num;

	int num_gpios;
	struct lge_gpio_entry *gpio_array;

	struct lge_panel_pin_seq *panel_on_seq;
	struct lge_panel_pin_seq *panel_off_seq;

	bool is_incell;
	bool flag_deep_sleep_ctrl;

	struct lge_blmap *blmap_list;
	int blmap_list_size;
	int default_brightness;
	bool panel_dead;
	int mfts_auto_touch;
	int shutdown_mode;

	//Display Bringup need to check is it need
	int sc_sat_step;
	int sc_hue_step;
	int sc_sha_step;

	/* For DISPLAY_COLOR_MANAGER */
	bool use_color_manager;
	u8 dgc_status;
	u8 sharpness_status;
	int screen_mode;
	int gc_mode;
	int cm_preset_step;
	int cm_red_step;
	int cm_green_step;
	int cm_blue_step;
	bool color_manager_default_status;
	int sharpness;

	/* for DISPLAY_HT_LCD_TUNE */
	int ht_lcd_tune;

	struct lge_ddic_ops *ddic_ops;
	/* FOR DISPLAY BACKLIGT CONTROL */
	bool allow_bl_update;
	int bl_lvl_unset;
	struct lge_ddic_dsi_panel_cmd_set lge_cmd_sets[LGE_DDIC_DSI_CMD_SET_MAX];
};

#endif //_H_LGE_DSI_PANEL_DEF_
