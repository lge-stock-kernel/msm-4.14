#define pr_fmt(fmt)	"[Display][ddic-ops:%s:%d] " fmt, __func__, __LINE__

#include "dsi_display.h"
#include "dsi_panel.h"

extern struct lge_ddic_ops sw49106_ops;
extern struct lge_ddic_ops sw49109_ops;

struct lge_ddic_match {
	char compatible[15];
	struct lge_ddic_ops *ops;
};

static struct lge_ddic_match supported_ddic_list[] = {
	{"sw49106", &sw49106_ops},
	{"sw49109", &sw49109_ops},
};

extern char* get_ddic_name(void);
extern bool is_ddic_name(char *ddic_name);

void lge_ddic_ops_init(struct dsi_panel *panel)
{
	int i;
	int count = sizeof(supported_ddic_list)/sizeof(supported_ddic_list[0]);

	for (i = 0; i < count; ++i) {
		if (is_ddic_name(supported_ddic_list[i].compatible)) {
			panel->lge.ddic_ops = supported_ddic_list[i].ops;
			break;
		}
	}

	if (panel->lge.ddic_ops == NULL)
		pr_warn("no matched ddic ops for %s\n", get_ddic_name());
}

void lge_ddic_feature_init(struct dsi_panel *panel)
{
	//empty
}

struct dsi_cmd_desc* find_nth_cmd(struct dsi_cmd_desc *cmds, int cmds_count, int addr, int nth)
{
	struct dsi_cmd_desc *ret = NULL;
	int i;
	char *payload;

	for (i = 0; i < cmds_count; ++i) {
		payload = (char*)cmds[i].msg.tx_buf;
		if (payload[0] == addr) {
			if (--nth == 0) {
				ret = &cmds[i];
				break;
			}
		}
	}

	return ret;
}

struct dsi_cmd_desc* find_cmd(struct dsi_cmd_desc *cmds, int cmds_count, int addr)
{
	struct dsi_cmd_desc *ret = NULL;
	int i;
	char *payload;

	for (i = 0; i < cmds_count; ++i) {
		payload = (char*)cmds[i].msg.tx_buf;
		if (payload[0] == addr) {
			ret = &cmds[i];
			break;
		}
	}

	return ret;
}

extern int dsi_panel_get_cmd_pkt_count(const char *data, u32 length, u32 *cnt);
extern int dsi_panel_create_cmd_packets(const char *data,
					u32 length,
					u32 count,
					struct dsi_cmd_desc *cmd);

char *lge_ddic_cmd_set_prop_map[LGE_DDIC_DSI_CMD_SET_MAX] = {
	"lge,mdss-dsi-disp-ctrl-command-1",
	"lge,mdss-dsi-disp-ctrl-command-2",
	"lge,digital-gamma-cmds-dummy",
	"lge,color-mode-cmds-dummy",
	"lge,color-mode-cmds-default",
	"lge,mdss-dsi-deep-enter-command",
	"lge,mdss-dsi-deep-exit-command",
	"lge,mdss-dsi-ht-tune-0-command",
	"lge,mdss-dsi-ht-tune-1-command",
	"lge,mdss-dsi-ht-tune-2-command",
	"lge,mdss-dsi-ht-tune-3-command",
	"lge,mdss-dsi-ht-tune-4-command",
};

char *lge_ddic_cmd_set_state_map[LGE_DDIC_DSI_CMD_SET_MAX] = {
	"lge,mdss-dsi-disp-ctrl-command-1-state",
	"lge,mdss-dsi-disp-ctrl-command-2-state",
	"lge,digital-gamma-cmds-dummy-state",
	"lge,color-mode-cmds-dummy-state",
	"lge,color-mode-cmds-default-state",
	"lge,mdss-dsi-deep-enter-command-state",
	"lge,mdss-dsi-deep-exit-command-state",
	"lge,mdss-dsi-ht-tune-0-command-state",
	"lge,mdss-dsi-ht-tune-1-command-state",
	"lge,mdss-dsi-ht-tune-2-command-state",
	"lge,mdss-dsi-ht-tune-3-command-state",
	"lge,mdss-dsi-ht-tune-4-command-state",
};

/* lge_ddic_dsi_panel_tx_cmd_set for LGE DSI CMD SETS*/
int lge_ddic_dsi_panel_tx_cmd_set(struct dsi_panel *panel,
				enum lge_ddic_dsi_cmd_set_type type)
{
	int rc = 0, i = 0;
	ssize_t len;
	struct dsi_cmd_desc *cmds;
	u32 count;
	enum dsi_cmd_set_state state;
	const struct mipi_dsi_host_ops *ops = panel->host->ops;

	if (!panel) {
		pr_err("panel is NULL\n");
		return -EINVAL;
	}

	if(!dsi_panel_initialized(panel)) {
		pr_err("panel not yet initialized\n");
		return -EINVAL;
	}

	cmds = panel->lge.lge_cmd_sets[type].cmds;
	count = panel->lge.lge_cmd_sets[type].count;
	state = panel->lge.lge_cmd_sets[type].state;

	if (count == 0) {
		pr_debug("[%s] No commands to be sent for state(%d)\n",
			 panel->name, type);
		goto error;
	}
	for (i = 0; i < count; i++) {
		if (state == DSI_CMD_SET_STATE_LP)
			cmds->msg.flags |= MIPI_DSI_MSG_USE_LPM;

		if (cmds->last_command)
			cmds->msg.flags |= MIPI_DSI_MSG_LASTCOMMAND;

		len = ops->transfer(panel->host, &cmds->msg);
		if (len < 0) {
			rc = len;
			pr_err("failed to set cmds(%d), rc=%d\n", type, rc);
			goto error;
		}
		if (cmds->post_wait_ms)
			usleep_range(cmds->post_wait_ms*1000,
					((cmds->post_wait_ms*1000)+10));
		cmds++;
	}
error:
	return rc;
}


int lge_ddic_dsi_panel_alloc_cmd_packets(struct lge_ddic_dsi_panel_cmd_set *cmd,
					u32 packet_count)
{
	u32 size;

	size = packet_count * sizeof(*cmd->cmds);
	cmd->cmds = kzalloc(size, GFP_KERNEL);
	if (!cmd->cmds)
		return -ENOMEM;

	cmd->count = packet_count;
	return 0;
}


int lge_ddic_dsi_panel_parse_cmd_sets_sub(struct lge_ddic_dsi_panel_cmd_set *cmd,
					enum lge_ddic_dsi_cmd_set_type type,
					struct device_node *of_node)
{
	int rc = 0;
	u32 length = 0;
	const char *data;
	const char *state;
	u32 packet_count = 0;

	data = of_get_property(of_node, lge_ddic_cmd_set_prop_map[type], &length);
	if (!data) {
		pr_debug("%s commands not defined\n", lge_ddic_cmd_set_prop_map[type]);
		rc = -ENOTSUPP;
		goto error;
	}

	rc = dsi_panel_get_cmd_pkt_count(data, length, &packet_count);
	if (rc) {
		pr_err("commands failed, rc=%d\n", rc);
		goto error;
	}
	pr_debug("[%s] packet-count=%d, %d\n", lge_ddic_cmd_set_prop_map[type],
		packet_count, length);

	rc = lge_ddic_dsi_panel_alloc_cmd_packets(cmd, packet_count);
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

	state = of_get_property(of_node, lge_ddic_cmd_set_state_map[type], NULL);
	if (!state || !strcmp(state, "dsi_lp_mode")) {
		cmd->state = DSI_CMD_SET_STATE_LP;
	} else if (!strcmp(state, "dsi_hs_mode")) {
		cmd->state = DSI_CMD_SET_STATE_HS;
	} else {
		pr_err("[%s] command state unrecognized-%s\n",
		       lge_ddic_cmd_set_state_map[type], state);
		goto error_free_mem;
	}

	return rc;
error_free_mem:
	kfree(cmd->cmds);
	cmd->cmds = NULL;
error:
	return rc;

}

int lge_ddic_dsi_panel_parse_cmd_sets(struct dsi_panel *panel,
	struct device_node *of_node)
{
	int rc = 0;
	struct lge_ddic_dsi_panel_cmd_set *set;
	u32 i;

	for(i = 0; i < LGE_DDIC_DSI_CMD_SET_MAX; i++) {
		set = &panel->lge.lge_cmd_sets[i];
		set->type = i;
		set->count = 0;
		rc = lge_ddic_dsi_panel_parse_cmd_sets_sub(set, i, of_node);
		if(rc)
			pr_err("parse set %d is failed or not defined\n", i);
	}
	rc = 0;
	return rc;
}

char* get_payload_addr(struct dsi_panel *panel, enum lge_ddic_dsi_cmd_set_type type, int position)
{
	struct lge_ddic_dsi_panel_cmd_set *cmd_set = NULL;
	struct dsi_cmd_desc *cmd = NULL;
	char *payload = NULL;

	if (type >= LGE_DDIC_DSI_CMD_SET_MAX) {
		pr_err("out of range\n");
		goto exit;
	}

	cmd_set = &(panel->lge.lge_cmd_sets[type]);
	if (cmd_set->count == 0) {
		pr_err("cmd set is not defined\n");
		goto exit;
	}

	cmd = &(panel->lge.lge_cmd_sets[type].cmds[position]);
	if (!cmd) {
		pr_err("empty cmd\n");
		goto exit;
	}

	payload = (char *)cmd->msg.tx_buf;
	if (!payload) {
		pr_err("empty payload\n");
		goto exit;
	}

	pr_debug("find payload\n");

exit:
	return payload;
}

int get_payload_cnt(struct dsi_panel *panel, enum lge_ddic_dsi_cmd_set_type type, int position)
{
	struct lge_ddic_dsi_panel_cmd_set *cmd_set = NULL;
	struct dsi_cmd_desc *cmd = NULL;
	int payload_count = 0;

	if (type >= LGE_DDIC_DSI_CMD_SET_MAX) {
		pr_err("out of range\n");
		goto exit;
	}

	cmd_set = &(panel->lge.lge_cmd_sets[type]);
	if (cmd_set->count == 0) {
		pr_err("cmd set is not defined\n");
		goto exit;
	}

	cmd = &(panel->lge.lge_cmd_sets[type].cmds[position]);
	if (!cmd) {
		pr_err("empty cmd\n");
		goto exit;
	}

	payload_count = (int)cmd->msg.tx_len;

	pr_debug("find payload\n");

exit:
	return payload_count;
}
