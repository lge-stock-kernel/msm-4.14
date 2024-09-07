#define pr_fmt(fmt)	"[Display][lge-dsi-panel:%s:%d] " fmt, __func__, __LINE__

#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <video/mipi_display.h>
#include <linux/kallsyms.h>
#include <linux/lge_panel_notify.h>
#include <soc/qcom/lge/board_lge.h>

#if IS_ENABLED(CONFIG_LGE_DISPLAY_EXTERNAL_DSV)
#define EXT_DSV_PRIVILEGED
#include <linux/mfd/external_dsv.h>
#endif
#include <linux/msm_lcd_power_mode.h>

#include "msm_drv.h"
#include "dsi_panel.h"
#include "dsi_ctrl_hw.h"
#include "dsi_display.h"
#include "sde_connector.h"
#include "sde_encoder.h"
#include <drm/drm_mode.h>
#include "dsi_drm.h"
#include "cm/lge_color_manager.h"

int dsi_panel_reset(struct dsi_panel *panel);
void lge_extra_gpio_set_value(struct dsi_panel *panel, const char *name, int value);
extern int lge_get_mfts_mode(void);
extern int dsi_panel_set_pinctrl_state(struct dsi_panel *panel, bool enable);
extern int dsi_panel_tx_cmd_set(struct dsi_panel *panel, enum dsi_cmd_set_type type);
extern int dsi_panel_get_cmd_pkt_count(const char *data, u32 length, u32 *cnt);
extern int dsi_panel_create_cmd_packets(const char *data,
					u32 length,
					u32 count,
					struct dsi_cmd_desc *cmd);
extern int dsi_panel_alloc_cmd_packets(struct dsi_panel_cmd_set *cmd,
					u32 packet_count);

extern void lge_dsi_panel_blmap_free(struct dsi_panel *panel);
extern int lge_dsi_panel_parse_blmap(struct dsi_panel *panel, struct device_node *of_node);
extern int lge_dsi_panel_parse_brightness(struct dsi_panel *panel,	struct device_node *of_node);
extern void lge_ddic_ops_init(struct dsi_panel *panel);
extern void lge_ddic_feature_init(struct dsi_panel *panel);
extern int lge_ddic_dsi_panel_parse_cmd_sets(struct dsi_panel *panel, struct device_node *of_node);
extern int lge_mdss_dsi_panel_cmd_read(struct dsi_panel *panel,
					u8 cmd, int cnt, char* ret_buf);
int lge_dsi_panel_pin_seq(struct lge_panel_pin_seq *seq);
extern int lge_ddic_dsi_panel_tx_cmd_set(struct dsi_panel *panel,
				enum lge_ddic_dsi_cmd_set_type type);
extern void lge_panel_reg_create_sysfs(struct dsi_panel *panel, struct class *class_panel);

#define MAN_NAME_LEN    10
#define	DDIC_NAME_LEN	15
static char lge_man_name[MAN_NAME_LEN+1];
static char lge_ddic_name[DDIC_NAME_LEN+1];

char* get_ddic_name(void)
{
	return lge_ddic_name;
}

bool is_ddic_name(char *ddic_name)
{
	if (ddic_name == NULL) {
		pr_err("input parameter is NULL\n");
		return false;
	}

	if(!strcmp(lge_ddic_name, ddic_name)) {
		return true;
	}
	pr_err("input ddic_name = %s, lge_ddic = %s\n", ddic_name, lge_ddic_name);
	return false;
}
EXPORT_SYMBOL(is_ddic_name);

/*---------------------------------------------------------------------------*/
/* LCD off & dimming                                                         */
/*---------------------------------------------------------------------------*/
static bool fb_blank_called;

bool is_blank_called(void)
{
	return fb_blank_called;
}

bool is_lcd_dimming_mode(void)
{
	/* if A/B update is used, fota reboot dimming is NOT required */
	if (lge_get_bootreason() == 0x23 || lge_get_bootreason() == 0x24)
		return true;
	if (lge_get_bootreason() == 0x25)
		return true;
	return false;
}

bool is_factory_cable(void)
{
	cable_boot_type cable_info = lge_get_boot_cable();

	if (cable_info == LT_CABLE_56K ||
		cable_info == LT_CABLE_130K ||
		cable_info == LT_CABLE_910K)
		return true;
	return false;
}

u32 lge_get_brightness(u32 bl_lvl)
{
	u32 brightness = bl_lvl;

	if (!is_blank_called() && is_lcd_dimming_mode() && bl_lvl > 0) {
		brightness = 1;
		pr_info("lcd dimming mode. set value = %d\n", brightness);
	} else if (!is_blank_called() && is_factory_cable() && bl_lvl > 0) {
		brightness =  1;
		pr_info("Detect factory cable. set value = %d\n", brightness);
	}
	return brightness;
}

void lge_set_blank_called(void)
{
	fb_blank_called = true;
}

int dsi_panel_full_power_seq(struct dsi_panel *panel)
{
	int ret = 0;

	if (!panel->lge.is_incell || lge_get_mfts_mode() || panel->lge.panel_dead)
		ret = 1;

	if (panel->lge.mfts_auto_touch)
		ret = 0;

	if (panel->lge.shutdown_mode)
		ret = 1;

	return ret;
}

static int dsi_panel_get_current_power_mode(struct dsi_panel *panel)
{
	struct dsi_display *display = NULL;
	struct sde_connector *conn = NULL;
	int mode;

	if (!panel) {
		pr_err("invalid panel param\n");
		return -EINVAL;
	}

	display = container_of(panel->host, struct dsi_display, host);
	if (!display) {
		pr_err("invalid display param\n");
		return -EINVAL;
	}

	conn = to_sde_connector(display->drm_conn);
	if (!conn)
		return -EINVAL;

	switch (conn->dpms_mode) {
	case DRM_MODE_DPMS_ON:
		mode = conn->lp_mode;
		break;
	case DRM_MODE_DPMS_STANDBY:
		mode = SDE_MODE_DPMS_STANDBY;
		break;
	case DRM_MODE_DPMS_SUSPEND:
		mode = SDE_MODE_DPMS_SUSPEND;
		break;
	case DRM_MODE_DPMS_OFF:
		mode = SDE_MODE_DPMS_OFF;
		break;
	default:
		mode = conn->lp_mode;
		pr_err("unrecognized mode=%d\n", mode);
		break;
	}

	return mode;
}

static inline bool is_power_off(int pwr_mode)
{
	return (pwr_mode == SDE_MODE_DPMS_OFF);
}

static inline bool is_power_on_interactive(int pwr_mode)
{
	return (pwr_mode == SDE_MODE_DPMS_ON);
}

static inline bool is_power_on(int pwr_mode)
{
	return !is_power_off(pwr_mode);
}

static inline bool is_power_on_lp(int pwr_mode)
{
	return !is_power_off(pwr_mode) &&
		!is_power_on_interactive(pwr_mode);
}

static inline bool is_power_on_ulp(int pwr_mode)
{
	return (pwr_mode == SDE_MODE_DPMS_LP2);
}

bool lge_dsi_panel_is_power_off(struct dsi_panel *panel)
{
	int last_panel_power_mode = dsi_panel_get_current_power_mode(panel);
	return is_power_off(last_panel_power_mode);
}

bool lge_dsi_panel_is_power_on_interactive(struct dsi_panel *panel)
{
	int last_panel_power_mode = dsi_panel_get_current_power_mode(panel);
	return is_power_on_interactive(last_panel_power_mode);
}

bool lge_dsi_panel_is_power_on(struct dsi_panel *panel)
{
	int last_panel_power_mode = dsi_panel_get_current_power_mode(panel);
	return is_power_on(last_panel_power_mode);
}

bool lge_dsi_panel_is_power_on_lp(struct dsi_panel *panel)
{
	int last_panel_power_mode = dsi_panel_get_current_power_mode(panel);
	return is_power_on_lp(last_panel_power_mode);
}

bool lge_dsi_panel_is_power_on_ulp(struct dsi_panel *panel)
{
	int last_panel_power_mode = dsi_panel_get_current_power_mode(panel);
	return is_power_on_ulp(last_panel_power_mode);
}

static void lge_mdss_panel_recovery_done(struct dsi_panel *panel)
{
	if (panel->lge.panel_dead) {
		panel->lge.panel_dead = false;
		lge_panel_notifier_call_chain(LGE_PANEL_EVENT_RECOVERY, 0, LGE_PANEL_RECOVERY_ALIVE);
		panel->lge.bl_lvl_unset = -1;
		panel->lge.allow_bl_update = true;
	}
}

static void lge_mdss_panel_dead_notify(struct dsi_display *display)
{
	struct sde_connector *conn = NULL;
	struct drm_event event;

	if (!display) {
		pr_err("display is null.\n");
		return;
	}

	if (!display->panel) {
		pr_err("panel is null.\n");
		return;
	}

	conn = to_sde_connector(display->drm_conn);
	if (!conn) {
		pr_err("display->drm_conn is null\n");
		return;
	}

	mutex_lock(&display->panel->panel_lock);
	if (display->panel->lge.panel_dead) {
		pr_err("Already in recovery state\n");
		mutex_unlock(&display->panel->panel_lock);
		return;
	}

	pr_info("******** ESD detected!!!!LCD recovery function called!!!! ********\n");
	display->panel->lge.panel_dead = true;

	mutex_unlock(&display->panel->panel_lock);
	lge_panel_notifier_call_chain(LGE_PANEL_EVENT_RECOVERY, 0, LGE_PANEL_RECOVERY_DEAD);

	event.type = DRM_EVENT_PANEL_DEAD;
	event.length = sizeof(u32);
	msm_mode_object_event_notify(&conn->base.base,
		conn->base.dev, &event, (u8 *)&display->panel->lge.panel_dead);
	sde_encoder_display_failure_notification(conn->encoder, false);
}

void lge_mdss_report_panel_dead(void)
{
	unsigned int **addr;
	struct dsi_display *display = NULL;
	struct sde_connector *conn = NULL;

	addr = (unsigned int **)kallsyms_lookup_name("primary_display");

	if (addr) {
		display = (struct dsi_display *)*addr;
	} else {
		pr_err("primary_display not founded\n");
		return;
	}

	if (!display->panel) {
		pr_err("panel is null.\n");
		return;
	}

	conn = to_sde_connector(display->drm_conn);
	if (!conn) {
		pr_err("display->drm_conn is null\n");
		return;
	}

	mutex_lock(&conn->lock);
	if (dsi_panel_get_current_power_mode(display->panel) != SDE_MODE_DPMS_ON) {
		pr_info("lp_state is not nolp(U3) - skip ESD recovery\n");
		mutex_unlock(&conn->lock);
		return;
	}
	mutex_unlock(&conn->lock);
	lge_mdss_panel_dead_notify(display);
}
EXPORT_SYMBOL(lge_mdss_report_panel_dead);

/* @Override */
int dsi_panel_power_on(struct dsi_panel *panel)
{
	int rc = 0;

	if (panel->lge.ddic_ops && panel->lge.ddic_ops->lge_dsi_panel_power_on){
		rc = panel->lge.ddic_ops->lge_dsi_panel_power_on(panel);
		return rc;
	}

	rc = dsi_pwr_enable_regulator(&panel->power_info, true);
	if (rc) {
		pr_err("[%s] failed to enable vregs, rc=%d\n", panel->name, rc);
		goto exit;
	}

	rc = dsi_panel_set_pinctrl_state(panel, true);
	if (rc) {
		pr_err("[%s] failed to set pinctrl, rc=%d\n", panel->name, rc);
		goto error_disable_vregs;
	}

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

error_disable_vregs:
	(void)dsi_pwr_enable_regulator(&panel->power_info, false);

exit:
	return rc;
}

int lge_dsi_panel_pin_seq(struct lge_panel_pin_seq *seq)
{
	int rc = 0;

	if (!seq) {
		return -EINVAL;
	}

	while (gpio_is_valid(seq->gpio)) {
		if (seq->level) {
			rc = gpio_direction_output(seq->gpio, 1);
			if (rc) {
				pr_err("unable to set dir for gpio %d, rc=%d\n", seq->gpio, rc);
				break;
			} else {
				pr_info("gpio %d -> %d\n", seq->gpio, 1);
			}
		} else {
			gpio_set_value(seq->gpio, 0);
			pr_info("gpio %d -> %d\n", seq->gpio, 0);
		}
		usleep_range(seq->sleep_ms*1000, seq->sleep_ms*1000);
		seq++;
	}

	return rc;
}

/* @Override */
int dsi_panel_power_off(struct dsi_panel *panel)
{
	int rc = 0;

	if (gpio_is_valid(panel->reset_config.disp_en_gpio))
		gpio_set_value(panel->reset_config.disp_en_gpio, 0);

	if (dsi_panel_full_power_seq(panel)) {
		if (gpio_is_valid(panel->reset_config.reset_gpio)){
			lge_panel_notifier_call_chain(LGE_PANEL_EVENT_RESET, 0, LGE_PANEL_RESET_LOW);
			gpio_set_value(panel->reset_config.reset_gpio, 0);
			usleep_range(5*1000, 5*1000);
		}
	} else {
		pr_err("[Display] Do not control LCD reset for LPWG mode");
	}

	if (gpio_is_valid(panel->reset_config.lcd_mode_sel_gpio))
		gpio_set_value(panel->reset_config.lcd_mode_sel_gpio, 0);

	rc = dsi_panel_set_pinctrl_state(panel, false);
	if (rc) {
		pr_err("[%s] failed set pinctrl state, rc=%d\n", panel->name,
				rc);
	}

#if IS_ENABLED(CONFIG_LGE_DISPLAY_EXTERNAL_DSV)
	if (dsi_panel_full_power_seq(panel)) {
		ext_dsv_mode_change(POWER_OFF);
		ext_dsv_chip_enable(0);
		usleep_range(5*1000, 5*1000);
	}
#endif
	if(panel->lge.pins) {
		if (dsi_panel_full_power_seq(panel)) {
			pr_err("[DEBUG] VDDIO OFF\n");
			rc = lge_dsi_panel_pin_seq(panel->lge.panel_off_seq);
			lge_panel_notifier_call_chain(LGE_PANEL_EVENT_POWER, 0, LGE_PANEL_POWER_VDDIO_OFF); // PANEL VDDIO LOW
		} else {
			pr_info("[Display] Do not control LCD powers for LPWG mode");
		}
	} else {
		rc = dsi_pwr_enable_regulator(&panel->power_info, false);
		if (rc)
			pr_err("[%s] failed to enable vregs, rc=%d\n", panel->name, rc);
	}
	lge_panel_notifier_call_chain(LGE_PANEL_EVENT_BLANK, 0, LGE_PANEL_STATE_BLANK); // U0, BLANK
	return rc;
}

/* @Override */
int dsi_panel_reset(struct dsi_panel *panel)
{
	int rc = 0;
	struct dsi_panel_reset_config *r_config = &panel->reset_config;
	int i;
	pr_err("[DEBUG] dsi_panel_reset\n");

	if (gpio_is_valid(panel->reset_config.disp_en_gpio)) {
		rc = gpio_direction_output(panel->reset_config.disp_en_gpio, 1);
		if (rc) {
			pr_err("unable to set dir for disp gpio rc=%d\n", rc);
			goto exit;
		}
	}

	if (r_config->count) {
		rc = gpio_direction_output(r_config->reset_gpio,
			r_config->sequence[0].level);
		if (rc) {
			pr_err("unable to set dir for rst gpio rc=%d\n", rc);
			goto exit;
		}
	}

	pr_info("Set Reset GPIO to HIGH\n");
	for (i = 0; i < r_config->count; i++) {
		gpio_set_value(r_config->reset_gpio,
			       r_config->sequence[i].level);

		if (r_config->sequence[i].sleep_ms)
			usleep_range(r_config->sequence[i].sleep_ms * 1000,
				     r_config->sequence[i].sleep_ms * 1000);
	}
	lge_panel_notifier_call_chain(LGE_PANEL_EVENT_RESET, 0, LGE_PANEL_RESET_HIGH);

	if (gpio_is_valid(panel->bl_config.en_gpio)) {
		rc = gpio_direction_output(panel->bl_config.en_gpio, 1);
		if (rc) {
			pr_err("unable to set dir for bklt gpio rc=%d\n", rc);
		}
	}

	if (gpio_is_valid(panel->reset_config.lcd_mode_sel_gpio)) {
		bool out = true;

		if ((panel->reset_config.mode_sel_state == MODE_SEL_DUAL_PORT)
				|| (panel->reset_config.mode_sel_state
					== MODE_GPIO_LOW))
			out = false;
		else if ((panel->reset_config.mode_sel_state
				== MODE_SEL_SINGLE_PORT) ||
				(panel->reset_config.mode_sel_state
				 == MODE_GPIO_HIGH))
			out = true;

		rc = gpio_direction_output(
			panel->reset_config.lcd_mode_sel_gpio, out);
		if (rc) {
			pr_err("unable to set dir for mode gpio rc=%d\n", rc);
		}
	}
exit:
	return rc;
}

/* @Override */
int dsi_panel_pre_prepare(struct dsi_panel *panel)
{
	int rc = 0;
	pr_err("[DEBUG] dsi_panel_pre_prepare\n");

	if (!panel) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	/* If LP11_INIT is set, panel will be powered up during prepare() */
	if (panel->lp11_init)
		goto error;

	rc = dsi_panel_power_on(panel);
	if (rc) {
		pr_err("[%s] panel power on failed, rc=%d\n", panel->name, rc);
		goto error;
	}

error:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

/* @Override */
int dsi_panel_prepare(struct dsi_panel *panel)
{
	int rc = 0;
	pr_err("[DEBUG] dsi_panel_prepare\n");

	if (!panel) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	if (panel->lp11_init) {
		rc = dsi_panel_power_on(panel);
		if (rc) {
			pr_err("[%s] panel power on failed, rc=%d\n",
					panel->name, rc);
			goto error;
		}
	}

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_PRE_ON);
	if (rc) {
		pr_err("[%s] failed to send DSI_CMD_SET_PRE_ON cmds, rc=%d\n",
		       panel->name, rc);
		goto error;
	}

error:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

/* @Override */
int dsi_panel_unprepare(struct dsi_panel *panel)
{
	int rc = 0;
	pr_err("[DEBUG] dsi_panel_unprepare\n");

	if (!panel) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);
	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_POST_OFF);
	if (rc) {
		pr_err("[%s] failed to send DSI_CMD_SET_POST_OFF cmds, rc=%d\n",
		       panel->name, rc);
		goto error;
	}

error:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

/* @Override */
int dsi_panel_post_unprepare(struct dsi_panel *panel)
{
	int rc = 0;
	pr_err("[DEBUG] dsi_panel_post_unprepare\n");

	if (!panel) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	rc = dsi_panel_power_off(panel);
	if (rc) {
		pr_err("[%s] panel power_Off failed, rc=%d\n", panel->name, rc);
		goto error;
	}
	panel->lge.flag_deep_sleep_ctrl =  true;

error:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

/* @Override */
int dsi_panel_post_enable(struct dsi_panel *panel)
{
	int rc = 0;
	pr_err("[DEBUG] dsi_panel_post_enable\n");

	if (!panel) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	lge_mdss_panel_recovery_done(panel);

	if (panel->lge.ddic_ops && panel->lge.ddic_ops->lge_dsi_panel_post_enable){
		rc = panel->lge.ddic_ops->lge_dsi_panel_post_enable(panel);
		return rc;
	}
	mutex_lock(&panel->panel_lock);

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_POST_ON);
	if (rc) {
		pr_err("[%s] failed to send DSI_CMD_SET_POST_ON cmds, rc=%d\n",
		       panel->name, rc);
		goto error;
	}
error:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

static int lge_dsi_panel_gpio_request(struct dsi_panel *panel)
{
	int rc = 0, i, j;
	char name[10] = {0,};

	for (i = 0; i < panel->lge.pins_num; i++) {
		if (gpio_is_valid(panel->lge.pins[i])) {
			snprintf(name, sizeof(name), "panel_pin_%d_gpio", i);
			rc = gpio_request(panel->lge.pins[i], name);
			if (rc) {
				pr_err("request for %s failed, rc=%d\n", name, rc);
				break;
			}
		}
	}

	if (i < panel->lge.pins_num) {
		for (j = i; j >= 0; j--) {
			if (gpio_is_valid(panel->lge.pins[j]))
				gpio_free(panel->lge.pins[j]);
		}
	}

	return rc;
}

static int gpio_name_to_index(struct dsi_panel *panel, const char *name)
{
	int i, index = -1;

	for (i = 0; i < panel->lge.num_gpios; ++i) {
		if (!strcmp(panel->lge.gpio_array[i].name, name)) {
			index = i;
			break;
		}
	}

	return index;
}

void lge_extra_gpio_set_value(struct dsi_panel *panel,
		const char *name, int value)
{
	int index = -1;

	index = gpio_name_to_index(panel, name);

	if (index != -1) {
		gpio_set_value(panel->lge.gpio_array[index].gpio, value);
	} else {
		pr_err("%s: couldn't get gpio by name %s\n", __func__, name);
	}
}

static int lge_dsi_panel_gpio_release(struct dsi_panel *panel)
{
	int rc = 0, i;
	for (i = 0; i < panel->lge.pins_num; i++) {
		if (gpio_is_valid(panel->lge.pins[i]))
			gpio_free(panel->lge.pins[i]);
	}
	return rc;
}

static ssize_t panel_type_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dsi_panel *panel = NULL;

	panel = dev_get_drvdata(dev);
	if (!panel) {
		pr_err("panel is NULL\n");
		return -EINVAL;
	}

	pr_err("%s-%s\n", lge_man_name, lge_ddic_name);

	/* The number of characters should not exceed 30 characters. */
	return sprintf(buf, "%s-%s\n", lge_man_name, lge_ddic_name);
}
static DEVICE_ATTR(panel_type, S_IRUGO,
		panel_type_show, NULL);

static ssize_t mfts_auto_touch_test_mode_get(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dsi_panel *panel;

	panel = dev_get_drvdata(dev);

	if (!panel) {
		pr_err("panel is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", panel->lge.mfts_auto_touch);
}

static ssize_t mfts_auto_touch_test_mode_set(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)

{
	struct dsi_panel *panel;
	int input;

	panel = dev_get_drvdata(dev);
	if (!panel) {
		pr_err("panel is NULL\n");
		return -EINVAL;
	}

	sscanf(buf, "%d", &input);
	mutex_lock(&panel->panel_lock);
	panel->lge.mfts_auto_touch = input;
	mutex_unlock(&panel->panel_lock);

	pr_info("auto touch test : %d\n", input);

	return size;
}
static DEVICE_ATTR(mfts_auto_touch_test_mode, S_IRUGO | S_IWUSR | S_IWGRP,
		mfts_auto_touch_test_mode_get, mfts_auto_touch_test_mode_set);

static ssize_t shutdown_mode_get(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dsi_panel *panel;

	panel = dev_get_drvdata(dev);

	if (!panel) {
		pr_err("panel is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", panel->lge.shutdown_mode);
}

static ssize_t shutdown_mode_set(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)

{
	struct dsi_panel *panel;
	int input;

	panel = dev_get_drvdata(dev);
	if (!panel) {
		pr_err("panel is NULL\n");
		return -EINVAL;
	}

	sscanf(buf, "%d", &input);
	mutex_lock(&panel->panel_lock);
	panel->lge.shutdown_mode = input;
	mutex_unlock(&panel->panel_lock);

	pr_info("shutdown : %d\n", input);

	return size;
}
static DEVICE_ATTR(shutdown_mode, S_IRUGO | S_IWUSR | S_IWGRP,
		shutdown_mode_get, shutdown_mode_set);

static ssize_t ht_lcd_tune_get(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dsi_panel *panel;

	panel = dev_get_drvdata(dev);

	if (!panel) {
		pr_err("panel is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", panel->lge.ht_lcd_tune);
}

ssize_t ht_lcd_tune_set(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct dsi_panel *panel;
	struct dsi_display *display;
	int input;

	panel = dev_get_drvdata(dev);
	if (!panel) {
		pr_err("panel is NULL\n");
		return -EINVAL;
	}
	display = container_of(panel->host, struct dsi_display, host);

	if (display->is_cont_splash_enabled) {
		pr_warn("%s: skip while cont splash is enabled\n", __func__);
		return -EINVAL;
	}

	sscanf(buf, "%d", &input);

	if (!dsi_panel_initialized(panel)) {
		pr_err("Panel off state. Ignore ht_lcd_tune set cmd but keep tune value\n");
		panel->lge.ht_lcd_tune = input;
		return -EINVAL;
	}

	panel->lge.ht_lcd_tune = input;

	if (panel->lge.ddic_ops && panel->lge.ddic_ops->lge_set_ht_lcd_tune)
		panel->lge.ddic_ops->lge_set_ht_lcd_tune(panel, panel->lge.ht_lcd_tune);

	return size;
}

static DEVICE_ATTR(ht_lcd_tune, S_IRUGO | S_IWUSR | S_IWGRP,
		ht_lcd_tune_get, ht_lcd_tune_set);



void lge_dsi_panel_factory_create_sysfs(struct dsi_panel *panel, struct class *class_panel)
{
	static struct device *panel_factory_dev = NULL;

	if (!panel_factory_dev) {
		panel_factory_dev = device_create(class_panel, NULL, 0, panel, "factory");
		if (IS_ERR(panel_factory_dev)) {
			pr_err("Failed to create panel_factory_dev!\n");
		} else {
			if ((device_create_file(panel_factory_dev, &dev_attr_panel_type)) < 0)
				pr_err("add panel_type node fail!\n");
			if ((device_create_file(panel_factory_dev, &dev_attr_mfts_auto_touch_test_mode)) < 0)
				pr_err("add mfts_auto_touch_test_mode node fail!\n");
			if ((device_create_file(panel_factory_dev, &dev_attr_shutdown_mode)) < 0)
				pr_err("add shutdown_mode node fail!\n");
		}
	}
}

static void lge_dsi_panel_create_sysfs(struct dsi_panel *panel)
{
	static struct class *class_panel = NULL;
	static struct device *panel_img_tune_sysfs_dev = NULL;
	int rc = 0;

	if (!class_panel) {
		class_panel = class_create(THIS_MODULE, "panel");
		if (IS_ERR(class_panel)) {
			pr_err("Failed to create panel class\n");
			return;
		}
	}
	if(!panel_img_tune_sysfs_dev){
		panel_img_tune_sysfs_dev = device_create(class_panel, NULL, 0, panel, "img_tune");
		if (IS_ERR(panel_img_tune_sysfs_dev)) {
			pr_err("Failed to create dev(panel_img_tune_sysfs_dev)!\n");
		} else {
			if (panel->lge.use_color_manager)
				lge_color_manager_create_sysfs(panel, panel_img_tune_sysfs_dev);
			if ((rc = device_create_file(panel_img_tune_sysfs_dev, &dev_attr_ht_lcd_tune)) < 0)
				pr_err("add ht_lcd_tune set node fail!");
		}
	}

	lge_dsi_panel_factory_create_sysfs(panel, class_panel);
	lge_panel_reg_create_sysfs(panel, class_panel);
}

int lge_dsi_panel_drv_init(struct dsi_panel *panel)
{
	int rc = 0;

	rc = lge_dsi_panel_gpio_request(panel);
	if (rc)
		pr_err("failed to request panel pins, rc=%d\n", rc);

	lge_dsi_panel_create_sysfs(panel);

	return rc;
}

int lge_dsi_panel_drv_post_init(struct dsi_panel *panel)
{
	int rc = 0;
	struct dsi_display *display = container_of(panel->host, struct dsi_display, host);

	pr_info("bl control variable init\n");
	if (display->is_cont_splash_enabled) {
		panel->lge.allow_bl_update = true;
		panel->lge.bl_lvl_unset = -1;
	} else {
		panel->lge.allow_bl_update = false;
		panel->lge.bl_lvl_unset = -1;
	}
	return rc;
}

int lge_dsi_panel_drv_deinit(struct dsi_panel *panel)
{
	int rc = 0;

	rc = lge_dsi_panel_gpio_release(panel);
	if (rc)
		pr_err("failed to release panel pins, rc=%d\n", rc);

	return rc;
}

static int lge_dsi_panel_parse_pin_seq(struct dsi_panel *panel, struct device_node *of_node, const char* prop_name, struct lge_panel_pin_seq **seq_out)
{
	int rc = 0, i;
	u32 length = 0, count = 0;
	const u32 *prop;
	u32 *arr;
	struct lge_panel_pin_seq *seq;

	prop = of_get_property(of_node, prop_name, &length);
	if (!prop) {
		pr_err("%s not found\n", prop_name);
		rc = -EINVAL;
		goto error_no_free_arr;
	}

	arr = kzalloc(length, GFP_KERNEL);
	if (!arr) {
		rc = -ENOMEM;
		goto error_no_free_arr;
	}

	length /= sizeof(u32);
	rc = of_property_read_u32_array(of_node, prop_name, arr, length);
	if (rc) {
		pr_err("failed to read u32 array of %s, rc=%d\n", prop_name, rc);
		goto error_free_arr;
	}

	count = length/3;
	seq = kzalloc(sizeof(struct lge_panel_pin_seq)*(count+1), GFP_KERNEL);
	if (!seq) {
		rc = -ENOMEM;
		goto error_free_arr;
	}

	*seq_out = seq;
	for (i = 0; i < length; i += 3) {
		if (arr[i] >= panel->lge.pins_num || arr[i] < 0) {
			pr_err("failed to parse %s, pins_num=%d, arr[%d]=%d\n", prop_name, panel->lge.pins_num, i, arr[i]);
			rc = -EINVAL;
			break;
		}
		seq->gpio = panel->lge.pins[arr[i]];
		seq->level = arr[i+1];
		seq->sleep_ms = arr[i+2];
		seq++;
	}
	seq->gpio = -1;

	if (rc) {
		kfree(*seq_out);
		*seq_out = NULL;
	}

error_free_arr:
	kfree(arr);
error_no_free_arr:
	return rc;
}

#define LGE_PROPNAME_PANEL_ON_PIN_SEQ "lge,panel-on-pin-seq"
#define LGE_PROPNAME_PANEL_OFF_PIN_SEQ "lge,panel-off-pin-seq"
#define LGE_PROPNAME_PANEL_PINS "lge,panel-pins"
static int lge_dsi_panel_parse_gpios(struct dsi_panel *panel, struct device_node *of_node)
{
	int rc = 0, i;

	panel->lge.pins_num = of_gpio_named_count(of_node, LGE_PROPNAME_PANEL_PINS);

	if (panel->lge.pins_num <= 0) {
		pr_warn("no panel pin defined\n");
		return 0;
	}

	panel->lge.pins = kcalloc(panel->lge.pins_num, sizeof(int), GFP_KERNEL);
	if (!panel->lge.pins) {
		rc = -ENOMEM;
		goto error_alloc_gpio_array;
	}

	for (i = 0; i < panel->lge.pins_num; i++) {
		panel->lge.pins[i] = of_get_named_gpio(of_node, LGE_PROPNAME_PANEL_PINS, i);
	}

	rc = lge_dsi_panel_parse_pin_seq(panel, of_node, LGE_PROPNAME_PANEL_ON_PIN_SEQ, &panel->lge.panel_on_seq);
	if (rc) {
		goto error_parse_pins;
	}

	rc = lge_dsi_panel_parse_pin_seq(panel, of_node, LGE_PROPNAME_PANEL_OFF_PIN_SEQ, &panel->lge.panel_off_seq);
	if (rc) {
		goto error_parse_pins;
	}

	return rc;

error_parse_pins:
	kfree(panel->lge.pins);
	panel->lge.pins = NULL;
	panel->lge.pins_num = 0;

error_alloc_gpio_array:
	return rc;
}

static int lge_dsi_panel_parse_gpios_params(struct dsi_panel *panel, struct device_node *of_node)
{
	int rc = 0, i;
	const char *name;
	char buf[256];

	struct dsi_display *display = NULL;

	display = container_of(panel->host, struct dsi_display, host);
	if (!display) {
		pr_err("invalid display param\n");
		return -EINVAL;
	}

	rc = of_property_count_strings(of_node, "lge,extra-gpio-names");
	if (rc > 0) {
		panel->lge.num_gpios = rc;
		pr_info("%s: num_gpios=%d\n", __func__, panel->lge.num_gpios);
		panel->lge.gpio_array = kmalloc(sizeof(struct lge_gpio_entry)*panel->lge.num_gpios, GFP_KERNEL);
		if (NULL == panel->lge.gpio_array) {
			pr_err("%s: no memory\n", __func__);
			panel->lge.num_gpios = 0;
			return -ENOMEM;
		}
		for (i = 0; i < panel->lge.num_gpios; ++i) {
			of_property_read_string_index(of_node, "lge,extra-gpio-names", i, &name);
			strlcpy(panel->lge.gpio_array[i].name, name, sizeof(panel->lge.gpio_array[i].name));
			snprintf(buf, sizeof(buf), "lge,gpio-%s", name);
			panel->lge.gpio_array[i].gpio = of_get_named_gpio(of_node, buf, 0);
			if (!gpio_is_valid(panel->lge.gpio_array[i].gpio))
				pr_err("%s: %s not specified\n", __func__, buf);
		}
	} else {
		panel->lge.num_gpios = 0;
		pr_info("%s: no lge specified gpio\n", __func__);
	}
	return 0;
}


static int lge_dsi_panel_parse_dt(struct dsi_panel *panel, struct device_node *of_node)
{
	int rc = 0;
	const char *ddic_name;
	const char *man_name;


	memset(lge_man_name, 0x0, MAN_NAME_LEN+1);
	man_name = of_get_property(of_node, "lge,man-name", NULL);
	if (man_name) {
		strncpy(lge_man_name, man_name, MAN_NAME_LEN);
		pr_info("lge_man_name=%s\n", lge_man_name);
	} else {
		strncpy(lge_man_name, "undefined", MAN_NAME_LEN);
		pr_info("manufacturer name is not set\n");
	}

	memset(lge_ddic_name, 0x0, DDIC_NAME_LEN+1);
	ddic_name = of_get_property(of_node, "lge,ddic-name", NULL);
	if (ddic_name) {
		strncpy(lge_ddic_name, ddic_name, DDIC_NAME_LEN);
		pr_info("lge_ddic_name=%s\n", lge_ddic_name);
	} else {
		strncpy(lge_ddic_name, "undefined", DDIC_NAME_LEN);
		pr_info("ddic name is not set\n");
	}

	panel->lge.is_incell = of_property_read_bool(of_node, "lge,incell-panel");
	pr_info("is_incell=%d\n", panel->lge.is_incell);

	panel->lge.use_color_manager = of_property_read_bool(of_node, "lge,use-color-manager");
	pr_info("use color manager=%d\n", panel->lge.use_color_manager);

	return rc;
}

int lge_dsi_panel_get(struct dsi_panel *panel, struct device_node *of_node)
{
	int rc = 0;

	rc = lge_dsi_panel_parse_gpios(panel, of_node);
	if (rc)
		pr_err("failed to parse panel gpios, rc=%d\n", rc);

	rc = lge_dsi_panel_parse_gpios_params(panel, of_node);
	if (rc)
		pr_err("failed to parse panel gpios, rc=%d\n", rc);

	rc = lge_dsi_panel_parse_blmap(panel, of_node);
	if (rc)
		pr_err("failed to parse blmap, rc=%d\n", rc);

	rc = lge_dsi_panel_parse_brightness(panel, of_node);
	if (rc)
		pr_err("failed to parse default brightness, rc=%d\n", rc);

	rc = lge_dsi_panel_parse_dt(panel, of_node);
	if (rc)
		pr_err("failed to parse dt, rc=%d\n", rc);

	rc = lge_ddic_dsi_panel_parse_cmd_sets(panel, of_node);
	if (rc)
		pr_err("failed to parse ddic cmds sets, rc=%d\n", rc);

	lge_ddic_ops_init(panel);

	lge_ddic_feature_init(panel);

	return rc;
}

inline void lge_dsi_panel_pin_seq_deinit(struct lge_panel_pin_seq **pseq)
{
	struct lge_panel_pin_seq *seq = *pseq;
	if (seq) {
		*pseq = NULL;
		kfree(seq);
	}
}

static void lge_dsi_panel_pins_deinit(struct dsi_panel *panel)
{
	if (panel->lge.pins_num && panel->lge.pins) {
		panel->lge.pins_num = 0;
		kfree(panel->lge.pins);
		lge_dsi_panel_pin_seq_deinit(&panel->lge.panel_on_seq);
		lge_dsi_panel_pin_seq_deinit(&panel->lge.panel_off_seq);
	}
}

void lge_dsi_panel_put(struct dsi_panel *panel)
{
	lge_dsi_panel_blmap_free(panel);
	lge_dsi_panel_pins_deinit(panel);
}

int lge_dsi_panel_parse_cmd_sets_sub(struct dsi_panel_cmd_set *cmd,
					const char *data,
					u32 length)
{
	int rc = 0;
	u32 packet_count = 0;

	rc = dsi_panel_get_cmd_pkt_count(data, length, &packet_count);
	if (rc) {
		pr_err("commands failed, rc=%d\n", rc);
		goto error;
	}

	rc = dsi_panel_alloc_cmd_packets(cmd, packet_count);
	if (rc) {
		pr_err("failed to allocate cmd packets, rc=%d\n", rc);
		goto error;
	}

	rc = dsi_panel_create_cmd_packets(data, length, packet_count,
					  cmd->cmds);
	if (rc) {
		pr_err("failed to create cmd packets, rc=%d\n", rc);
		goto error_free_mem;
	}

	return rc;
error_free_mem:
	kfree(cmd->cmds);
	cmd->cmds = NULL;
error:
	return rc;
}

int lge_dsi_panel_tx_cmd_set(struct dsi_panel *panel,
				struct dsi_panel_cmd_set *cmd)
{
	int rc = 0, i = 0, j=0;
	ssize_t len;
	struct dsi_cmd_desc *cmds;

	u32 count;
	enum dsi_cmd_set_state state;
	const struct mipi_dsi_host_ops *ops = panel->host->ops;


	if (!panel || !panel->cur_mode)
		return -EINVAL;

	cmds = cmd->cmds;
	count = cmd->count;
	state = cmd->state;

	if (count == 0) {
		pr_debug("[%s] No commands to be sent\n",
			 panel->name);
		goto error;
	}

	for (i = 0; i < count; i++) {
		/* TODO:  handle last command */
		if (state == DSI_CMD_SET_STATE_LP)
			cmds->msg.flags |= MIPI_DSI_MSG_USE_LPM;

		if (cmds->last_command)
			cmds->msg.flags |= MIPI_DSI_MSG_LASTCOMMAND;

		len = ops->transfer(panel->host, &cmds->msg);
		if (len < 0) {
			rc = len;
			pr_err("failed to set cmds, rc=%d\n", rc);
			goto error;
		}
		if (cmds->post_wait_ms)
			usleep_range(cmds->post_wait_ms * 1000, ((cmds->post_wait_ms * 1000) + 10));
		for(j=0; j < cmd->cmds[i].msg.tx_len; j++)
		{
			pr_debug("0x%02x send\n", (*(u8 *)(cmd->cmds[i].msg.tx_buf+j)));
		}
		cmds++;
	}
error:
	return rc;
}

int lge_mdss_dsi_panel_cmd_read(struct dsi_panel *panel, u8 cmd, int cnt, char* ret_buf)
{
	u8 rx_buf[256] = {0x0};
	int i = 0, ret = 0, checksum = 0;
	const struct mipi_dsi_host_ops *ops;

	struct dsi_cmd_desc cmds = {
		.msg = {
			.channel = 0,
			.type = MIPI_DSI_DCS_READ,
			.tx_buf = &cmd,
			.tx_len = 1,
			.rx_buf = &rx_buf[0],
			.rx_len = cnt,
			.flags = MIPI_DSI_MSG_USE_LPM | MIPI_DSI_MSG_LASTCOMMAND | MIPI_DSI_MSG_REQ_ACK
		},
		.last_command = false,
		.post_wait_ms = 0,
	};

	/* TO DO : panel connection check */
	/* if (not_connected) return -EINVAL */

	if (!panel) {
		pr_err("panel is NULL\n");
		return -EINVAL;
	}

	ops = panel->host->ops;
	ret = ops->transfer(panel->host, &cmds.msg);

	for (i = 0; i < cnt; i++)
		checksum += rx_buf[i];

	pr_info("[Reg:0x%02x] checksum=%d\n", cmd, checksum);

	memcpy(ret_buf, rx_buf, cnt);

	return checksum;
}

void lge_panel_enter_deep_sleep(struct dsi_panel *panel)
{
	if (panel->lge.ddic_ops && panel->lge.ddic_ops->lge_panel_enter_deep_sleep){
		panel->lge.ddic_ops->lge_panel_enter_deep_sleep(panel);
		return;
	}
}

void lge_panel_exit_deep_sleep(struct dsi_panel *panel)
{
	if (panel->lge.ddic_ops && panel->lge.ddic_ops->lge_panel_exit_deep_sleep){
		panel->lge.ddic_ops->lge_panel_exit_deep_sleep(panel);
		return;
	}
}


void lge_panel_set_power_mode(int mode)
{
	unsigned int **addr;
	struct dsi_display *display = NULL;
	struct dsi_panel *panel = NULL;

	addr = (unsigned int **)kallsyms_lookup_name("primary_display");

	if (addr) {
		display = (struct dsi_display *)*addr;
	} else {
		pr_err("primary_display not founded\n");
		return;
	}
	panel = display -> panel;
	if (!panel) {
		pr_err("panel is NULL\n");
		return;
	}
	pr_info("%s start, mode = %d \n", __func__, mode);

	switch (mode){
		case DSV_TOGGLE:
			ext_dsv_mode_change(ENM_ENTER);
			break;
		case DSV_ALWAYS_ON:
			ext_dsv_mode_change(ENM_EXIT);
			break;
		case DEEP_SLEEP_ENTER:
			lge_panel_enter_deep_sleep(panel);
			break;
		case DEEP_SLEEP_EXIT:
			lge_panel_exit_deep_sleep(panel);
			break;
		default :
			break;
	}

	pr_info("%s done \n", __func__);
	return;
}
