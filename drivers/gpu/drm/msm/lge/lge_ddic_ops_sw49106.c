#define pr_fmt(fmt)	"[Display][sw49106-ops:%s:%d] " fmt, __func__, __LINE__

#include "dsi_panel.h"
#include "lge_ddic_ops_helper.h"
#include <linux/lge_panel_notify.h>
#include "cm/lge_color_manager.h"

#if IS_ENABLED(CONFIG_LGE_DISPLAY_EXTERNAL_DSV)
#define EXT_DSV_PRIVILEGED
#include <linux/mfd/external_dsv.h>
#endif

#define WORDS_TO_BYTE_ARRAY(w1, w2, b) do {\
		b[0] = WORD_UPPER_BYTE(w1);\
		b[1] = WORD_LOWER_BYTE(w1);\
		b[2] = WORD_UPPER_BYTE(w2);\
		b[3] = WORD_LOWER_BYTE(w2);\
} while(0)

extern int lge_ddic_dsi_panel_tx_cmd_set(struct dsi_panel *panel,
				enum lge_ddic_dsi_cmd_set_type type);
extern char* get_payload_addr(struct dsi_panel *panel, enum lge_ddic_dsi_cmd_set_type type, int position);
extern int get_payload_cnt(struct dsi_panel *panel, enum lge_ddic_dsi_cmd_set_type type, int position);
extern int dsi_panel_full_power_seq(struct dsi_panel *panel);
extern int lge_dsi_panel_pin_seq(struct lge_panel_pin_seq *seq);
extern int dsi_panel_set_pinctrl_state(struct dsi_panel *panel, bool enable);
extern int dsi_panel_reset(struct dsi_panel *panel);
extern int dsi_panel_tx_cmd_set(struct dsi_panel *panel, enum dsi_cmd_set_type type);

#define IDX_DG_CTRL1 0
#define REG_DG_CTRL1 0xF4
#define NUM_DG_CTRL1 16

#define IDX_DG_CTRL2 1
#define REG_DG_CTRL2 0xF5
#define NUM_DG_CTRL2 16

#define IDX_DG_CTRL3 2
#define REG_DG_CTRL3 0xF6
#define NUM_DG_CTRL3 16

#define IDX_CM_CTRL1 0
#define IDX_CM_CTRL2 1

#define STEP_DG_PRESET 5

#define STEP_GC_PRESET 4

#define NUM_DG_PRESET  7
#define OFFSET_DG_CTRL 8

#define NUM_SHA_CTRL 		5
#define REG_SHA_CTRL 0xF2

#define NUM_SAT_CTRL 		5
#define REG_SAT_CTRL 0xF3
#define OFFSET_SAT_CTRL 	6

#define NUM_HUE_CTRL 		5
#define REG_HUE_CTRL 0xF3
#define OFFSET_HUE_CTRL 	2

#define SC_MODE_DEFAULT 2
#define SHARP_DEFAULT   0

#define PRESET_SETP0_OFFSET 0
#define PRESET_SETP1_OFFSET 1
#define PRESET_SETP2_OFFSET 2

enum {
	screen_mode_auto = 0,
	screen_mode_cinema = 1,
	screen_mode_sports = 4,
	screen_mode_game = 5,
	screen_mode_expert = 10,
};

static char sha_ctrl_values[NUM_SHA_CTRL] = {0x00, 0x0D, 0x1A, 0x30, 0xD2};

static char sat_ctrl_values[NUM_SAT_CTRL][OFFSET_SAT_CTRL] = {
	{0x00, 0x44, 0x7B, 0xAC, 0xDB, 0x00},
	{0x00, 0x4A, 0x85, 0xBA, 0xED, 0x00},
	{0x00, 0x50, 0x90, 0xC9, 0x00, 0x01},
	{0x00, 0x54, 0x98, 0xD5, 0x00, 0x01},
	{0x00, 0x60, 0xA0, 0xD9, 0x00, 0x01},
};

static char hue_ctrl_values[NUM_HUE_CTRL][OFFSET_HUE_CTRL] = {
	{0xF7, 0x00},
	{0xF4, 0x00},
	{0xF0, 0x00},
	{0xE4, 0x00},
	{0xE7, 0x00},
};

static int rgb_preset[STEP_DG_PRESET][RGB_ALL] = {
	{PRESET_SETP2_OFFSET, PRESET_SETP0_OFFSET, PRESET_SETP2_OFFSET},
	{PRESET_SETP2_OFFSET, PRESET_SETP1_OFFSET, PRESET_SETP2_OFFSET},
	{PRESET_SETP0_OFFSET, PRESET_SETP0_OFFSET, PRESET_SETP0_OFFSET},
	{PRESET_SETP0_OFFSET, PRESET_SETP1_OFFSET, PRESET_SETP0_OFFSET},
	{PRESET_SETP0_OFFSET, PRESET_SETP2_OFFSET, PRESET_SETP0_OFFSET}
};

static int gc_preset[STEP_GC_PRESET][RGB_ALL] = {
	{0x00, 0x00, 0x00},
	{0x00, 0x00, 0x06},
	{0x03, 0x00, 0x00},
	{0x00, 0x00, 0x00},
};

static char dg_ctrl_values[NUM_DG_PRESET][OFFSET_DG_CTRL] = {
	{0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40},
	{0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F},
	{0x3E, 0x3E, 0x3E, 0x3E, 0x3E, 0x3E, 0x3E, 0x3E},
	{0x3D, 0x3D, 0x3D, 0x3D, 0x3D, 0x3D, 0x3D, 0x3D},
	{0x3C, 0x3C, 0x3C, 0x3C, 0x3C, 0x3C, 0x3C, 0x3C},
	{0x3B, 0x3B, 0x3B, 0x3B, 0x3B, 0x3B, 0x3B, 0x3B},
	{0x3A, 0x3A, 0x3A, 0x3A, 0x3A, 0x3A, 0x3A, 0x3A}
};

int lge_panel_power_on_sw49106(struct dsi_panel *panel)
{
	int rc = 0;

	if(panel->lge.pins) {
		if (dsi_panel_full_power_seq(panel)) {
			pr_err("[DEBUG] VDDIO ON\n");
			rc = lge_dsi_panel_pin_seq(panel->lge.panel_on_seq);
			if (rc){
				pr_err("[%s] failed to set lge panel pin, rc=%d\n", panel->name, rc);
				goto error_pinctrl_false;
			}
			lge_panel_notifier_call_chain(LGE_PANEL_EVENT_POWER, 0, LGE_PANEL_POWER_VDDIO_ON); // PANEL VDDIO ON
		} else {
			pr_err("[Display] Do not control display powers for Incell display");
		}
	} else {
		rc = dsi_pwr_enable_regulator(&panel->power_info, true);
		if (rc) {
			pr_err("[%s] failed to enable vregs, rc=%d\n", panel->name, rc);
			goto exit;
		}
	}

	rc = dsi_panel_set_pinctrl_state(panel, true);
	if (rc) {
		pr_err("[%s] failed to set pinctrl, rc=%d\n", panel->name, rc);
		goto error_disable_vregs;
	}
#if IS_ENABLED(CONFIG_LGE_DISPLAY_EXTERNAL_DSV)
	if (dsi_panel_full_power_seq(panel)) {
		pr_err("[DEBUG] dsv chip enable + POWER ON1\n");
		ext_dsv_chip_enable(1);
		ext_dsv_mode_change(POWER_ON_1);
	}
#endif
	mdelay(2);

	rc = dsi_panel_reset(panel);
	if (rc) {
		pr_err("[%s] failed to reset panel, rc=%d\n", panel->name, rc);
		goto error_disable_gpio;
	}

	goto exit;
error_disable_gpio:
	if (gpio_is_valid(panel->reset_config.disp_en_gpio))
		gpio_set_value(panel->reset_config.disp_en_gpio, 0);

	if (gpio_is_valid(panel->bl_config.en_gpio))
		gpio_set_value(panel->bl_config.en_gpio, 0);

	(void)dsi_panel_set_pinctrl_state(panel, false);

error_pinctrl_false:
	(void)dsi_panel_set_pinctrl_state(panel, false);

error_disable_vregs:
	(void)dsi_pwr_enable_regulator(&panel->power_info, false);
exit:
	return rc;
}

int lge_dsi_panel_post_enable_sw49106(struct dsi_panel *panel)
{
	int rc = 0;

	if (!panel) {
		pr_err("invalid params\n");
		return -EINVAL;
	}
#if IS_ENABLED(CONFIG_LGE_DISPLAY_EXTERNAL_DSV)
	if (dsi_panel_full_power_seq(panel)) {
		pr_err("[DEBUG]dsv_mode change POWER_ON_2\n");
		ext_dsv_mode_change(POWER_ON_2);
		mdelay(1);
	} else {
		ext_dsv_mode_change(ENM_EXIT);
	}
#endif
	mutex_lock(&panel->panel_lock);
	pr_err("[DEBUG] POST ON CMD\n");
	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_POST_ON);
	if (rc) {
		pr_err("[%s] failed to send DSI_CMD_SET_POST_ON cmds, rc=%d\n",
				panel->name, rc);
		goto error;
	}

error:
	mutex_unlock(&panel->panel_lock);
	if (panel->lge.use_color_manager) {
		if (panel->lge.ddic_ops && panel->lge.ddic_ops->lge_set_screen_mode)
			panel->lge.ddic_ops->lge_set_screen_mode(panel, true);
	}
	panel->lge.flag_deep_sleep_ctrl = false;
	lge_panel_notifier_call_chain(LGE_PANEL_EVENT_BLANK, 0, LGE_PANEL_STATE_UNBLANK); // U3, UNBLANK:
	return rc;
}

void lge_panel_enter_deep_sleep_sw49106(struct dsi_panel *panel)
{
	int rc = 0;

	if (panel->lge.flag_deep_sleep_ctrl) {
		mdelay(6);
		ext_dsv_mode_change(POWER_OFF);
		ext_dsv_chip_enable(0);

		if (gpio_is_valid(panel->reset_config.reset_gpio)){
			gpio_set_value(panel->reset_config.reset_gpio, 0);
		}
		mdelay(2);
		rc = lge_dsi_panel_pin_seq(panel->lge.panel_off_seq);
		mdelay(1);
		pr_info("%s done \n", __func__);
	}
}

void lge_panel_exit_deep_sleep_sw49106(struct dsi_panel *panel)
{
	int rc = 0;
	if (panel->lge.flag_deep_sleep_ctrl) {
		rc = lge_dsi_panel_pin_seq(panel->lge.panel_on_seq);
		if (rc){
			pr_err("[%s] failed to set lge panel pin, rc=%d\n", panel->name, rc);
			return;
		}
		mdelay(2);
		pr_err("[DEBUG] dsv chip enable + POWER ON1\n");
		ext_dsv_chip_enable(1);
		ext_dsv_mode_change(POWER_ON_1);

		if (gpio_is_valid(panel->reset_config.reset_gpio)){
			gpio_set_value(panel->reset_config.reset_gpio, 1);
		}
		mdelay(6);
		pr_err("[DEBUG]dsv_mode change POWER_ON_2\n");
		ext_dsv_mode_change(POWER_ON_2);
		mdelay(5);
		ext_dsv_mode_change(ENM_ENTER);
		pr_info("%s done \n", __func__);
	}

}

static void lge_set_custom_rgb_sw49106(struct dsi_panel *panel, bool send_cmd)
{
	int i = 0;
	int red_index, green_index, blue_index = 0;
	char *dgctl1_payload = NULL;
	char *dgctl2_payload = NULL;
	char *dgctl3_payload = NULL;

	enum lge_gamma_correction_mode cur_gc_mode = LGE_GC_MOD_NOR;

	if (panel == NULL) {
		pr_err("Invalid input\n");
		return;
	}

	mutex_lock(&panel->panel_lock);

	cur_gc_mode = panel->lge.gc_mode;
	if (cur_gc_mode == LGE_GC_MOD_NOR) {
		//Color temperature and RGB should be considered simultaneously.
		red_index   = rgb_preset[panel->lge.cm_preset_step][RED] + panel->lge.cm_red_step;
		green_index = rgb_preset[panel->lge.cm_preset_step][GREEN] + panel->lge.cm_green_step;
		blue_index  = rgb_preset[panel->lge.cm_preset_step][BLUE] + panel->lge.cm_blue_step;
	} else {
		red_index   = gc_preset[cur_gc_mode][RED];
		green_index = gc_preset[cur_gc_mode][GREEN];
		blue_index  = gc_preset[cur_gc_mode][BLUE];
	}

	pr_info("red_index=(%d) green_index=(%d) blue_index=(%d)\n", red_index, green_index, blue_index);

	dgctl1_payload = get_payload_addr(panel, LGE_DDIC_DSI_DISP_DG_COMMAND_DUMMY, IDX_DG_CTRL1);
	dgctl2_payload = get_payload_addr(panel, LGE_DDIC_DSI_DISP_DG_COMMAND_DUMMY, IDX_DG_CTRL2);
	dgctl3_payload = get_payload_addr(panel, LGE_DDIC_DSI_DISP_DG_COMMAND_DUMMY, IDX_DG_CTRL3);

	if (!dgctl1_payload || !dgctl2_payload || !dgctl3_payload) {
		pr_err("LGE_DDIC_DSI_DISP_DG_COMMAND_DUMMY is NULL\n");
		mutex_unlock(&panel->panel_lock);
		return;
	}

	for (i = 0; i < OFFSET_DG_CTRL; i++) {
		dgctl1_payload[i+1] = dg_ctrl_values[red_index][i];
		dgctl2_payload[i+1] = dg_ctrl_values[green_index][i];
		dgctl3_payload[i+1] = dg_ctrl_values[blue_index][i];
	}

	for (i = 0; i < NUM_DG_CTRL1; i++) {
		pr_debug("Reg:0x%02x [%d:0x%02x]\n", REG_DG_CTRL1, i, dgctl1_payload[i]);
	}

	for (i = 0; i < NUM_DG_CTRL2; i++) {
		pr_debug("Reg:0x%02x [%d:0x%02x]\n", REG_DG_CTRL2, i, dgctl2_payload[i]);
	}

	for (i = 0; i < NUM_DG_CTRL3; i++) {
		pr_debug("Reg:0x%02x [%d:0x%02x]\n", REG_DG_CTRL3, i, dgctl3_payload[i]);
	}

	if (send_cmd) {
		lge_ddic_dsi_panel_tx_cmd_set(panel, LGE_DDIC_DSI_DISP_DG_COMMAND_DUMMY);
	}

	mutex_unlock(&panel->panel_lock);
	return;
}

static void lge_display_control_store_sw49106(struct dsi_panel *panel, bool send_cmd)
{
	char *dispctrl1_payload = NULL;
	char *dispctrl2_payload = NULL;

	if(!panel) {
		pr_err("panel not exist\n");
		return;
	}

	mutex_lock(&panel->panel_lock);


	dispctrl1_payload = get_payload_addr(panel, LGE_DDIC_DSI_DISP_CTRL_COMMAND_1, 0);
	dispctrl2_payload = get_payload_addr(panel, LGE_DDIC_DSI_DISP_CTRL_COMMAND_2, 0);

	if (!dispctrl1_payload || !dispctrl2_payload) {
		pr_err("LGE_DDIC_DSI_DISP_CTRL_COMMAND is NULL\n");
		mutex_unlock(&panel->panel_lock);
		return;
	}

	/* CM_SEL & CE_SEL */
	dispctrl2_payload[1] &= 0x82;

	switch (panel->lge.screen_mode) {
		case screen_mode_expert:
			/* enable hue status */
			dispctrl2_payload[1] |= 0x40;
			break;
	}

	/* dgc_status */
	dispctrl2_payload[1] |= panel->lge.dgc_status << 3;

	pr_info("ctrl-command-1: 0x%02x 0x%02x", dispctrl1_payload[0], dispctrl1_payload[1]);
	pr_info("ctrl-command-2: 0x%02x 0x%02x\n", dispctrl2_payload[0], dispctrl2_payload[1]);

	if (send_cmd) {
		lge_ddic_dsi_panel_tx_cmd_set(panel, LGE_DDIC_DSI_DISP_CTRL_COMMAND_1);
		lge_ddic_dsi_panel_tx_cmd_set(panel, LGE_DDIC_DSI_DISP_CTRL_COMMAND_2);
	}

	mutex_unlock(&panel->panel_lock);
	return;
}
static void lge_set_screen_tune_sw49106(struct dsi_panel *panel)
{
	int i;
	int saturation_index = 0;
	int hue_index = 0;
	int sha_index = 0;

	char *cmctl1_payload = NULL;
	char *cmctl2_payload = NULL;

	mutex_lock(&panel->panel_lock);

	cmctl1_payload = get_payload_addr(panel, LGE_DDIC_DSI_DISP_CM_COMMAND_DUMMY, IDX_CM_CTRL1);
	cmctl2_payload = get_payload_addr(panel, LGE_DDIC_DSI_DISP_CM_COMMAND_DUMMY, IDX_CM_CTRL2);

	if (!cmctl1_payload || !cmctl2_payload) {
		pr_err("LGE_DDIC_DSI_DISP_CM_COMMAND_DUMMY is NULL\n");
		mutex_unlock(&panel->panel_lock);
		return;
	}

	saturation_index = panel->lge.sc_sat_step;
	hue_index = panel->lge.sc_hue_step;
	sha_index = panel->lge.sc_sha_step;

	if(panel->lge.screen_mode != screen_mode_expert)
	{
		saturation_index = SC_MODE_DEFAULT;
		hue_index = SC_MODE_DEFAULT;
		sha_index = SHARP_DEFAULT;

		if(panel->lge.screen_mode == screen_mode_game)
			saturation_index = 3; //Color Saturation +10%
	}
	pr_info("saturation_index=(%d) hue_index=(%d) sha_index=(%d)\n", saturation_index, hue_index, sha_index);

	// SATURATION CTRL
	for (i = 0; i < OFFSET_SAT_CTRL; i++) {
		cmctl2_payload[i+1] = sat_ctrl_values[saturation_index][i];
	}

	// HUE CTRL
	for (i = 0; i < OFFSET_HUE_CTRL; i++) {
		cmctl2_payload[i+7] = hue_ctrl_values[hue_index][i];
	}

	// SHARPNESS CTRL
	cmctl1_payload[3] = sha_ctrl_values[sha_index];

	for (i = 0; i < OFFSET_SAT_CTRL; i++) {
		pr_debug("Reg:0x%02x [%d:0x%02x]\n", REG_SAT_CTRL, i, cmctl2_payload[i+1]);
	}
	for (i = 0; i < OFFSET_HUE_CTRL; i++) {
		pr_debug("Reg:0x%02x [%d:0x%02x]\n", REG_HUE_CTRL, i, cmctl2_payload[i+7]);
	}
	pr_debug("Reg:0x%02x [%d:0x%02x]\n", REG_SHA_CTRL, i, cmctl1_payload[3]);

	lge_ddic_dsi_panel_tx_cmd_set(panel, LGE_DDIC_DSI_DISP_CM_COMMAND_DUMMY);

	mutex_unlock(&panel->panel_lock);

	return;
}

static void lge_set_screen_mode_sw49106(struct dsi_panel *panel, bool send_cmd)
{
	panel->lge.gc_mode = LGE_GC_MOD_NOR;
	mutex_lock(&panel->panel_lock);

	pr_info("screen_mode %d\n", panel->lge.screen_mode);

	switch (panel->lge.screen_mode) {
		case screen_mode_auto:
			pr_info("preset: %d, red: %d, green: %d, blue: %d\n",
					panel->lge.cm_preset_step, panel->lge.cm_red_step,
					panel->lge.cm_green_step, panel->lge.cm_blue_step);

			if (panel->lge.cm_preset_step == 2 &&
					!(panel->lge.cm_red_step | panel->lge.cm_green_step | panel->lge.cm_blue_step)) {
				panel->lge.dgc_status = 0x00;
			} else {
				panel->lge.dgc_status = 0x01;
				mutex_unlock(&panel->panel_lock);
				lge_set_custom_rgb_sw49106(panel, send_cmd);
				mutex_lock(&panel->panel_lock);
			}
			panel->lge.sharpness_status = 0x00;
			lge_ddic_dsi_panel_tx_cmd_set(panel, LGE_DDIC_DSI_DISP_CM_COMMAND_DEFAULT);
			break;
		case screen_mode_cinema:
			panel->lge.gc_mode = LGE_GC_MOD_CIN;
			panel->lge.dgc_status = 0x01;
			panel->lge.sharpness_status = 0x00;
			mutex_unlock(&panel->panel_lock);
			lge_set_custom_rgb_sw49106(panel, send_cmd);
			lge_ddic_dsi_panel_tx_cmd_set(panel, LGE_DDIC_DSI_DISP_CM_COMMAND_DEFAULT);
			mutex_lock(&panel->panel_lock);
			break;
		case screen_mode_sports:
			panel->lge.gc_mode = LGE_GC_MOD_SPO;
			panel->lge.dgc_status = 0x01;
			panel->lge.sharpness_status = 0x00;
			mutex_unlock(&panel->panel_lock);
			lge_set_custom_rgb_sw49106(panel, send_cmd);
			lge_ddic_dsi_panel_tx_cmd_set(panel, LGE_DDIC_DSI_DISP_CM_COMMAND_DEFAULT);
			mutex_lock(&panel->panel_lock);
			break;
		case screen_mode_game:
			panel->lge.gc_mode = LGE_GC_MOD_GAM;
			panel->lge.dgc_status = 0x01;
			panel->lge.sharpness_status = 0x00;
			mutex_unlock(&panel->panel_lock);
			lge_set_custom_rgb_sw49106(panel, send_cmd);
			lge_set_screen_tune_sw49106(panel);  //saturation update
			mutex_lock(&panel->panel_lock);
			break;
		case screen_mode_expert:
			pr_info("saturation: %d, hue: %d, sharpness: %d\n",
					panel->lge.sc_sat_step, panel->lge.sc_hue_step, panel->lge.sc_sha_step);

			panel->lge.dgc_status = 0x01;
			panel->lge.sharpness_status = 0x01;

			mutex_unlock(&panel->panel_lock);
			lge_set_custom_rgb_sw49106(panel, send_cmd);
			lge_set_screen_tune_sw49106(panel);
			mutex_lock(&panel->panel_lock);
			break;
		default:
			break;
	}

	mutex_unlock(&panel->panel_lock);
	lge_display_control_store_sw49106(panel, send_cmd);
	return;
}

struct lge_ddic_ops sw49106_ops = {
	.lge_dsi_panel_power_on = lge_panel_power_on_sw49106,
	.lge_dsi_panel_post_enable = lge_dsi_panel_post_enable_sw49106,
	.lge_panel_enter_deep_sleep = lge_panel_enter_deep_sleep_sw49106,
	.lge_panel_exit_deep_sleep = lge_panel_exit_deep_sleep_sw49106,
	.lge_display_control_store = lge_display_control_store_sw49106,
	.lge_set_custom_rgb = lge_set_custom_rgb_sw49106,
	.lge_set_screen_tune = lge_set_screen_tune_sw49106,
	.lge_set_screen_mode = lge_set_screen_mode_sw49106,
};
