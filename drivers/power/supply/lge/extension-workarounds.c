#define pr_fmt(fmt) "WA: %s: " fmt, __func__
#define pr_wa(fmt, ...) pr_err(fmt, ##__VA_ARGS__)
#define pr_dbg_wa(fmt, ...) pr_debug(fmt, ##__VA_ARGS__)

#include <linux/delay.h>
#include <linux/pmic-voter.h>
#include <linux/power_supply.h>
#include <linux/regulator/consumer.h>

#include "../qcom/smb5-reg.h"
#include "../qcom/fg-core.h"
#undef DEBUG_BOARD_VOTER
#include "../qcom/smb5-lib.h"
#include "veneer-primitives.h"
#ifdef CONFIG_LGE_USB_SBU_SWITCH
#include <linux/usb/lge_sbu_switch.h>
#endif

////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Helper functions
////////////////////////////////////////////////////////////////////////////
static struct smb_charger* wa_helper_chg(void) {
	// getting smb_charger from air
	struct power_supply*	psy
		= power_supply_get_by_name("battery");
	struct smb_charger*	chg
		= psy ? power_supply_get_drvdata(psy) : NULL;
	if (psy)
		power_supply_put(psy);

	return chg;
}

#define APSD_RERUN_DELAY_MS 4000
static DEFINE_MUTEX(wa_lock);
static bool wa_command_apsd(/*@Nonnull*/ struct smb_charger* chg) {
	bool	ret = false;
	int rc;

	mutex_lock(&wa_lock);
	rc = smblib_masked_write(chg, CMD_APSD_REG,
				APSD_RERUN_BIT, APSD_RERUN_BIT);
	if (rc < 0) {
		pr_wa("Couldn't re-run APSD rc=%d\n", rc);
		goto failed;
	}
	ret = true;

failed:
	mutex_unlock(&wa_lock);
	return ret;
}

#define USBIN_CMD_ICL_OVERRIDE_REG (USBIN_BASE + 0x42)
#define ICL_OVERRIDE_BIT BIT(0)
bool wa_command_icl_override(/*@Nonnull*/ struct smb_charger* chg) {
	if (smblib_masked_write(chg, USBIN_CMD_ICL_OVERRIDE_REG, ICL_OVERRIDE_BIT, ICL_OVERRIDE_BIT) < 0) {
		pr_wa("Couldn't icl override\n");
		return false;
	}

	return true;
}

static void wa_get_pmic_dump_func(struct work_struct *unused) {
	struct smb_charger* chg = wa_helper_chg();
	union power_supply_propval debug  = {-1, };

	if (!chg) {
		pr_wa("'chg' is not ready\n");
		return;
	}

	power_supply_set_property(chg->batt_psy,
			POWER_SUPPLY_PROP_DEBUG_BATTERY, &debug);
}
static DECLARE_WORK(wa_get_pmic_dump_work, wa_get_pmic_dump_func);

void wa_get_pmic_dump(void) {
	schedule_work(&wa_get_pmic_dump_work);
}

bool wa_command_chgtype(enum power_supply_type pst) {
#ifdef CONFIG_LGE_PM_VENEER_PSY
	struct power_supply* veneer = power_supply_get_by_name("veneer");

	if (veneer) {
		union power_supply_propval chgtype = { .intval = pst, };
		power_supply_set_property(veneer, POWER_SUPPLY_PROP_REAL_TYPE, &chgtype);
		power_supply_changed(veneer);
		power_supply_put(veneer);
		pr_info("[W/A] PSD-!) Setting charger type to %d by force\n", pst);

		return true;
	}
#endif
	return false;
}

////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Avoiding MBG fault on SBU pin
////////////////////////////////////////////////////////////////////////////

#ifdef CONFIG_LGE_USB_SBU_SWITCH
// Rather than accessing pointer directly, Referring it as a singleton instance
static struct lge_sbu_switch_instance* wa_avoiding_mbg_fault_singleton(void) {
	static struct lge_sbu_switch_desc 	wa_amf_description = {
		.flags  = LGE_SBU_SWITCH_FLAG_SBU_AUX
			| LGE_SBU_SWITCH_FLAG_SBU_USBID
			| LGE_SBU_SWITCH_FLAG_SBU_FACTORY_ID,
	};
	static struct lge_sbu_switch_instance*	wa_amf_instance;
	static DEFINE_MUTEX(wa_amf_mutex);
	struct smb_charger* chg;

	if (IS_ERR_OR_NULL(wa_amf_instance)) {
		mutex_lock(&wa_amf_mutex);
		chg = wa_helper_chg();
		if (IS_ERR_OR_NULL(wa_amf_instance) && chg) {
			wa_amf_instance
				= devm_lge_sbu_switch_instance_register(chg->dev, &wa_amf_description);
			if (IS_ERR_OR_NULL(wa_amf_instance))
				devm_lge_sbu_switch_instance_unregister(chg->dev, wa_amf_instance);
		}
		mutex_unlock(&wa_amf_mutex);
	}

	return IS_ERR_OR_NULL(wa_amf_instance) ? NULL : wa_amf_instance;
}

bool wa_avoiding_mbg_fault_uart(bool enable) {
	// Preparing instance and checking validation of it.
	struct lge_sbu_switch_instance* instance
		= wa_avoiding_mbg_fault_singleton();
	if (!instance)
		return false;

	if (enable) {
		if (lge_sbu_switch_get_current_flag(instance) != LGE_SBU_SWITCH_FLAG_SBU_AUX)
			lge_sbu_switch_get(instance, LGE_SBU_SWITCH_FLAG_SBU_AUX);
	}
	else
		lge_sbu_switch_put(instance, LGE_SBU_SWITCH_FLAG_SBU_AUX);

	return true;
}

bool wa_avoiding_mbg_fault_usbid(bool enable) {
	// Preparing instance and checking validation of it.
	struct lge_sbu_switch_instance* instance
		= wa_avoiding_mbg_fault_singleton();
	if (!instance)
		return false;

	if (enable)
		lge_sbu_switch_get(instance, LGE_SBU_SWITCH_FLAG_SBU_FACTORY_ID);
	else
		lge_sbu_switch_put(instance, LGE_SBU_SWITCH_FLAG_SBU_FACTORY_ID);

	return true;
}
#else
bool wa_avoiding_mbg_fault_uart(bool enable) { return false; };
bool wa_avoiding_mbg_fault_usbid(bool enable) { return false; };
#endif


////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Detection of Standard HVDCP2
////////////////////////////////////////////////////////////////////////////

#define DSH_VOLTAGE_THRESHOLD  7000
static bool wa_is_standard_hvdcp = false;
static bool wa_detect_standard_hvdcp_done = false;
static void wa_detect_standard_hvdcp_main(struct work_struct *unused) {
	struct smb_charger*  chg = wa_helper_chg();
	union power_supply_propval	val = {0, };
	int rc, usb_vnow;
	wa_detect_standard_hvdcp_done = true;

	if (!chg) {
		pr_wa("'chg' is not ready\n");
		return;
	}

	rc = smblib_dp_dm(chg, POWER_SUPPLY_DP_DM_FORCE_9V);
	if (rc < 0) {
		pr_wa("Couldn't force 9V rc=%d\n", rc);
		return;
	}

	msleep(200);
	usb_vnow = !smblib_get_prop_usb_voltage_now(chg, &val) ? val.intval/1000 : -1;
	if ( usb_vnow >= DSH_VOLTAGE_THRESHOLD) {
		wa_is_standard_hvdcp = true;
	}

	pr_wa("Check standard hvdcp. %d mV\n", usb_vnow);
	rc = smblib_dp_dm(chg, POWER_SUPPLY_DP_DM_FORCE_5V);
	if (rc < 0) {
		pr_wa("Couldn't force 5v rc=%d\n", rc);
		return;
	}

	return;
}
static DECLARE_DELAYED_WORK(wa_detect_standard_hvdcp_dwork, wa_detect_standard_hvdcp_main);

void wa_detect_standard_hvdcp_trigger(struct smb_charger* chg) {
	u8 stat;
	int rc;

	rc = smblib_read(chg, APSD_STATUS_REG, &stat);
	if (rc < 0) {
		pr_wa("Couldn't read APSD_STATUS_REG rc=%d\n", rc);
		return;
	}

	pr_dbg_wa("apsd_status 0x%x, type %d\n", stat, chg->real_charger_type);
	if ((stat & QC_AUTH_DONE_STATUS_BIT)
			&& chg->real_charger_type == POWER_SUPPLY_TYPE_USB_HVDCP
			&& !delayed_work_pending(&wa_detect_standard_hvdcp_dwork)
			&& !wa_detect_standard_hvdcp_done) {
		schedule_delayed_work(&wa_detect_standard_hvdcp_dwork, msecs_to_jiffies(0));
	}
}

void wa_detect_standard_hvdcp_clear(void) {
	wa_is_standard_hvdcp = false;
	wa_detect_standard_hvdcp_done = false;
}

bool wa_detect_standard_hvdcp_check(void) {
	return wa_is_standard_hvdcp;
}


////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Rerun apsd for dcp charger
////////////////////////////////////////////////////////////////////////////

static bool wa_rerun_apsd_done = false;

static void wa_rerun_apsd_for_dcp_main(struct work_struct *unused) {
	struct smb_charger* chg = wa_helper_chg();

	if (!chg || chg->pd_active ||!wa_rerun_apsd_done) {
		pr_wa("stop apsd done. apsd(%d), pd(%d)\n", wa_rerun_apsd_done, chg ? chg->pd_active : -1);
		return;
	}

	pr_wa(" Rerun apsd\n");
	wa_command_apsd(chg);
}

static DECLARE_DELAYED_WORK(wa_rerun_apsd_for_dcp_dwork, wa_rerun_apsd_for_dcp_main);

void wa_rerun_apsd_for_dcp_trigger(struct smb_charger *chg) {
	union power_supply_propval val = { 0, };
	bool usb_type_dcp = chg->real_charger_type == POWER_SUPPLY_TYPE_USB_DCP;
	bool usb_vbus_high
		= !power_supply_get_property(chg->usb_psy, POWER_SUPPLY_PROP_PRESENT, &val) ? !!val.intval : false;
	u8 stat;
	int rc;

	rc = smblib_read(chg, APSD_STATUS_REG, &stat);
	if (rc < 0) {
		pr_wa("Couldn't read APSD_STATUS_REG rc=%d\n", rc);
		return;
	}

	pr_dbg_wa("legacy(%d), done(%d), TO(%d), DCP(%d), Vbus(%d)\n",
		chg->typec_legacy, wa_rerun_apsd_done, stat, usb_type_dcp, usb_vbus_high);

	if (chg->typec_legacy && !wa_rerun_apsd_done
			&& (stat & HVDCP_CHECK_TIMEOUT_BIT) && usb_type_dcp && usb_vbus_high) {
		wa_rerun_apsd_done = true;
		schedule_delayed_work(&wa_rerun_apsd_for_dcp_dwork,
			round_jiffies_relative(msecs_to_jiffies(APSD_RERUN_DELAY_MS)));
	}
}

void wa_rerun_apsd_for_dcp_clear(void) {
	wa_rerun_apsd_done = false;
}


////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Charging without CC
////////////////////////////////////////////////////////////////////////////

/* CWC has two works for APSD and HVDCP, and this implementation handles the
 * works independently with different delay.
 * but you can see that retrying HVDCP detection work is depends on rerunning
 * APSD. i.e, APSD work derives HVDCP work.
 */
#define CWC_DELAY_MS  1000
static bool wa_charging_without_cc_processed = false;
static bool wa_charging_without_cc_required(struct smb_charger *chg) {
	union power_supply_propval val = { 0, };
	bool pd_hard_reset, usb_vbus_high, typec_mode_none,	wa_required;

	if (!chg) {
		pr_wa("'chg' is not ready\n");
		return false;
	}

	pd_hard_reset = chg->pd_hard_reset;
	usb_vbus_high = !power_supply_get_property(chg->usb_psy, POWER_SUPPLY_PROP_PRESENT, &val)
		? !!val.intval : false;
	typec_mode_none = chg->typec_mode == POWER_SUPPLY_TYPEC_NONE;

	wa_required = !pd_hard_reset && usb_vbus_high && typec_mode_none;
	if (!wa_required)
		pr_dbg_wa("Don't need CWC (pd_hard_reset:%d, usb_vbus_high:%d, typec_mode_none:%d)\n",
			pd_hard_reset, usb_vbus_high, typec_mode_none);

	return wa_required;
}

static void wa_charging_without_cc_main(struct work_struct *unused) {
	struct smb_charger*  chg = wa_helper_chg();

	if (wa_charging_without_cc_required(chg)) {
		pr_wa("CC line is not recovered until now, Start W/A\n");
		wa_charging_without_cc_processed = true;
		chg->typec_legacy = true;
		smblib_hvdcp_detect_enable(chg, true);
		smblib_rerun_apsd_if_required(chg);
		wa_command_icl_override(chg);
	}

	return;
}
static DECLARE_DELAYED_WORK(wa_charging_without_cc_dwork, wa_charging_without_cc_main);

void wa_charging_without_cc_trigger(struct smb_charger* chg, bool vbus) {
	// This may be triggered in the IRQ context of the USBIN rising.
	// So main function to start 'charging without cc', is deferred via delayed_work of kernel.
	// Just check and register (if needed) the work in this call.

	if (vbus && wa_charging_without_cc_required(chg)) {
		if (delayed_work_pending(&wa_charging_without_cc_dwork)) {
			pr_wa(" Cancel the pended trying apsd . . .\n");
			cancel_delayed_work(&wa_charging_without_cc_dwork);
		}

		schedule_delayed_work(&wa_charging_without_cc_dwork,
			msecs_to_jiffies(CWC_DELAY_MS));
	} else if (!vbus && wa_charging_without_cc_processed) {
		wa_charging_without_cc_processed = false;
		cancel_delayed_work(&wa_charging_without_cc_dwork);
		pr_wa("Call typec_removal by force\n");
		extension_typec_src_removal(chg);
	}
}

bool wa_charging_without_cc_is_running(void) {
	return wa_charging_without_cc_processed;
}


////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Rerun apsd for unknown charger
////////////////////////////////////////////////////////////////////////////
static void wa_charging_for_unknown_cable_main(struct work_struct *unused);
static DECLARE_DELAYED_WORK(wa_charging_for_unknown_cable_dwork, wa_charging_for_unknown_cable_main);

#define FLOAT_SETTING_DELAY_MS	1000
static void wa_charging_for_unknown_cable_main(struct work_struct *unused) {
	struct smb_charger*  chg = wa_helper_chg();
	struct power_supply* veneer = power_supply_get_by_name("veneer");
	union power_supply_propval floated
		= { .intval = POWER_SUPPLY_TYPE_USB_FLOAT, };
	union power_supply_propval val = { 0, };
	bool pd_hard_reset, usb_type_unknown, moisture_detected, usb_vbus_high,
			workaround_required, apsd_done, typec_mode_sink, ok_to_pd;
	bool vbus_valid = false;
	u8 stat;
	int rc;

	if (!chg) {
		pr_wa("'chg' is not ready\n");
		goto out_charging_for_unknown;
	}

	rc = smblib_read(chg, APSD_STATUS_REG, &stat);
	if (rc < 0) {
		pr_wa("Couldn't read APSD_STATUS_REG rc=%d\n", rc);
		goto out_charging_for_unknown;
	}
	apsd_done = (stat & APSD_DTC_STATUS_DONE_BIT);
#ifdef CONFIG_LGE_USB_MOISTURE_DETECTION
	if (!(*chg->lpd_ux)) {
		if (chg->lpd_reason == LPD_MOISTURE_DETECTED) {
			if (*chg->lpd_dpdm_disable) {
				floated.intval = POWER_SUPPLY_TYPE_USB_DCP;
			} else {
				floated.intval = POWER_SUPPLY_TYPE_USB;
			}
		}
	}
#endif
	rc = smblib_read(chg, TYPE_C_MISC_STATUS_REG, &stat);
	if (rc < 0) {
		pr_wa("Couldn't read TYPE_C_MISC_STATUS_REG rc=%d\n", rc);
		goto out_charging_for_unknown;
	}
	typec_mode_sink = (stat & SNK_SRC_MODE_BIT);

	pd_hard_reset = chg->pd_hard_reset;
	usb_type_unknown = chg->real_charger_type == POWER_SUPPLY_TYPE_UNKNOWN;
	moisture_detected
		= !power_supply_get_property(chg->usb_psy, POWER_SUPPLY_PROP_MOISTURE_DETECTED, &val)
		? !!val.intval : false;
	usb_vbus_high
		= !power_supply_get_property(chg->usb_psy, POWER_SUPPLY_PROP_PRESENT, &val)
		? true : false;
	ok_to_pd = chg->ok_to_pd;

	workaround_required = !pd_hard_reset
				&& !typec_mode_sink
				&& usb_type_unknown
				&& usb_vbus_high
				&& !ok_to_pd;
#ifdef CONFIG_LGE_USB_MOISTURE_DETECTION
	if (*chg->lpd_ux)
		workaround_required = workaround_required && !moisture_detected;
#endif
	if (!workaround_required) {
		pr_dbg_wa("check(!(pd_hard_reset:%d, MD:%d, typec_mode_sink:%d)"
			" usb_type_unknown:%d, usb_vbus_high:%d, ok_to_pd:%d,"
			" apsd_done:%d wa_charging_cc:%d, pending work:%d)\n",
			pd_hard_reset, moisture_detected, typec_mode_sink,
			usb_type_unknown, usb_vbus_high, ok_to_pd, apsd_done,
			wa_charging_without_cc_processed,
			delayed_work_pending(&wa_charging_without_cc_dwork));

		goto out_charging_for_unknown;
	}

	if (apsd_done && !delayed_work_pending(&wa_charging_without_cc_dwork)) {
		rc = smblib_write(chg, USBIN_ADAPTER_ALLOW_CFG_REG,
				USBIN_ADAPTER_ALLOW_5V);
		if (rc < 0) {
			pr_wa("Couldn't write 0x%02x to"
					" USBIN_ADAPTER_ALLOW_CFG_REG rc=%d\n",
					USBIN_ADAPTER_ALLOW_CFG_REG, rc);
			goto out_charging_for_unknown;
		}
		vbus_valid = !power_supply_get_property(chg->usb_psy,
				POWER_SUPPLY_PROP_PRESENT, &val)
				? !!val.intval : false;
		if (vbus_valid) {
			if (veneer) {
				pr_wa("Force setting cable as FLOAT if it is UNKNOWN after APSD\n");
				power_supply_set_property(veneer,
						POWER_SUPPLY_PROP_REAL_TYPE, &floated);
				power_supply_changed(veneer);
			} else {
				pr_wa("'veneer_psy' is not ready\n");
			}
		}
		else {
			pr_wa("VBUS is not valid\n");
		}
	}
	else {
		schedule_delayed_work(&wa_charging_for_unknown_cable_dwork,
			round_jiffies_relative(msecs_to_jiffies(FLOAT_SETTING_DELAY_MS)));
	}

out_charging_for_unknown:
	if (veneer)
		power_supply_put(veneer);
}

void wa_charging_for_unknown_cable_trigger(struct smb_charger* chg) {
	union power_supply_propval val = { 0, };
	bool usb_vbus_high
		= !power_supply_get_property(chg->usb_psy, POWER_SUPPLY_PROP_PRESENT, &val)
		? true : false;
	if (usb_vbus_high
		&& (wa_charging_without_cc_processed ||
			delayed_work_pending(&wa_charging_without_cc_dwork))) {
		schedule_delayed_work(&wa_charging_for_unknown_cable_dwork,
			round_jiffies_relative(msecs_to_jiffies(FLOAT_SETTING_DELAY_MS)));
	}
}


////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Support for weak battery pack
////////////////////////////////////////////////////////////////////////////

#define WEAK_SUPPLY_VOTER "WEAK_SUPPLY_VOTER"
#define WEAK_DELAY_MS		500
#define WEAK_DETECTION_COUNT	3
#define DEFAULT_WEAK_ICL_MA 1000

#define POWER_PATH_MASK		GENMASK(2, 1)
#define POWER_PATH_BATTERY	BIT(1)
#define POWER_PATH_USB		BIT(2)

static int  wa_support_weak_supply_count = 0;
static bool wa_support_weak_supply_running = false;

static void wa_support_weak_supply_func(struct work_struct *unused) {
	struct smb_charger* chg = wa_helper_chg();
	u8 stat;

	if (!wa_support_weak_supply_running)
		return;

	if (chg && !smblib_read(chg, POWER_PATH_STATUS_REG, &stat)) {
		if ((stat & POWER_PATH_MASK) == POWER_PATH_USB) {
			wa_support_weak_supply_count++;
			pr_wa("wa_support_weak_supply_count = %d\n",
				wa_support_weak_supply_count);
			if (wa_support_weak_supply_count >= WEAK_DETECTION_COUNT) {
				pr_wa("Weak battery is detected, set ICL to 1A\n");
				vote(chg->usb_icl_votable, WEAK_SUPPLY_VOTER,
					true, DEFAULT_WEAK_ICL_MA*1000);
			}
		}
	}
	wa_support_weak_supply_running = false;
}

static DECLARE_DELAYED_WORK(wa_support_weak_supply_dwork, wa_support_weak_supply_func);

void wa_support_weak_supply_trigger(struct smb_charger* chg, u8 stat) {
	bool trigger = !!(stat & USE_USBIN_BIT);

	if ((stat & POWER_PATH_MASK) != POWER_PATH_BATTERY)
		return;

	if (trigger) {
		if (!delayed_work_pending(&wa_support_weak_supply_dwork))
			schedule_delayed_work(&wa_support_weak_supply_dwork,
				round_jiffies_relative(msecs_to_jiffies(WEAK_DELAY_MS)));
	}
	else if (!!wa_support_weak_supply_count) {
		pr_wa("Clear wa_support_weak_supply_count\n");
		wa_support_weak_supply_count = 0;
		vote(chg->usb_icl_votable, WEAK_SUPPLY_VOTER, false, 0);
	}
	else
		; /* Do nothing */
}

void wa_support_weak_supply_check(void) {
	if (delayed_work_pending(&wa_support_weak_supply_dwork))
		wa_support_weak_supply_running = true;
}


////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Resuming Suspended USBIN
////////////////////////////////////////////////////////////////////////////

#define AICL_FAIL_BIT				BIT(1)
#define AICL_RERUN_TIME_MASK			GENMASK(1, 0)
#define AICL_RERUN_DELAY_MS 3500
#define INITIAL_DELAY_MS 5000

static bool wa_resuming_suspended_usbin_required(void) {
	// Checking condition in prior to recover usbin suspending
	struct smb_charger*  chg = wa_helper_chg();
	u8 reg_status_aicl, reg_status_powerpath, reg_status_rt;

	if (chg && (get_effective_result_locked(chg->usb_icl_votable) != 0)
			&& (smblib_read(chg, AICL_STATUS_REG, &reg_status_aicl) >= 0)
			&& (smblib_read(chg, POWER_PATH_STATUS_REG, &reg_status_powerpath) >= 0)
			&& (smblib_read(chg, USBIN_BASE + INT_RT_STS_OFFSET, &reg_status_rt) >= 0) ) {
		pr_dbg_wa("AICL_STATUS_REG(0x%04x):0x%02x,"
			" POWER_PATH_STATUS_REG(0x%04x):0x%02x"
			" USB_INT_RT_STS(0x%04x):0x%02x\n",
			AICL_STATUS_REG, reg_status_aicl,
			POWER_PATH_STATUS_REG, reg_status_powerpath,
			USBIN_BASE + INT_RT_STS_OFFSET, reg_status_rt);

		if (reg_status_rt & USBIN_PLUGIN_RT_STS_BIT) {
			if ((reg_status_aicl & AICL_FAIL_BIT)
				|| (reg_status_powerpath & USBIN_SUSPEND_STS_BIT)) {
				pr_wa("AICL_FAIL:%d, USBIN_SUSPEND:%d\n",
					!!(reg_status_aicl & AICL_FAIL_BIT),
					!!(reg_status_powerpath & USBIN_SUSPEND_STS_BIT));
				return true;
			}
		}
		else
			pr_dbg_wa("[W/A] RSU-?) Skip because USB is not present\n");
	}

	return false;
}

static void wa_resuming_suspended_usbin_func(struct work_struct *unused) {
// 0. Local variables
	// References for charger driver
	struct smb_charger* chg = wa_helper_chg();
	int 		    irq = (chg && chg->irq_info) ? chg->irq_info[USBIN_ICL_CHANGE_IRQ].irq : 0;
	// Buffer to R/W PMI register
	int		    ret;
	u8		    buf;

	// Previous values to be restored
	int pre_usbin_collapse;
	int pre_aicl_rerun_time;

	if (!chg || !wa_resuming_suspended_usbin_required()) {
		pr_wa("Exiting recovery for USBIN-suspend. (%p)\n", chg);
		return;
	}
	else {
		pr_wa("Start resuming suspended usbin\n");
	}

// 1. W/A to prevent the IRQ 'usbin-icl-change' storm (CN#03165535) on SDM845
	// : Before recovery USBIN-suspend, be sure that IRQ 'usbin-icl-change' is enabled.
	//   If not, this recovery will not work well due to the disabled AICL notification.
	// : To prevent IRQ 'usbin-icl-change' storm, it might be disabled in its own ISR.
	//   Refer to the disabling IRQ condition in 'smblib_handle_icl_change()'
	ret = smblib_read(chg, POWER_PATH_STATUS_REG, &buf);
	if (irq && ret >= 0 && (buf & USBIN_SUSPEND_STS_BIT) && !chg->usb_icl_change_irq_enabled) {
		enable_irq(irq);
		chg->usb_icl_change_irq_enabled = true;
		pr_wa("USBIN_SUSPEND_STS_BIT = High, Enable ICL-CHANGE IRQ\n");
	}
	else {
		pr_wa("irq_number=%d, irq_enabled=%d, read_return=%d, read_register=%d\n",
			irq, chg->usb_icl_change_irq_enabled, ret, buf);
	}

// 2. Toggling USBIN_CMD_IL_REG
	pr_wa("Toggling USBIN_CMD_IL_REG(0x1340[0]) := 1\n");
	if (smblib_masked_write(chg, USBIN_CMD_IL_REG, USBIN_SUSPEND_BIT, USBIN_SUSPEND_BIT) < 0) {
		pr_wa("Couldn't write suspend to USBIN_SUSPEND_BIT\n");
		goto failed;
	}

	pr_wa("Toggling USBIN_CMD_IL_REG(0x1340[0]) := 0\n");
	if (smblib_masked_write(chg, USBIN_CMD_IL_REG, USBIN_SUSPEND_BIT, 0) < 0) {
		pr_wa("Couldn't write resume to USBIN_SUSPEND_BIT\n");
		goto failed;
	}

// 3. Save origial AICL configurations
	if (smblib_read(chg, USBIN_AICL_OPTIONS_CFG_REG /*0x1380*/, &buf) >= 0) {
		pre_usbin_collapse = buf & SUSPEND_ON_COLLAPSE_USBIN_BIT;
		pr_wa("USBIN_AICL_OPTIONS_CFG_REG=0x%02x, SUSPEND_ON_COLLAPSE_USBIN_BIT=0x%02x\n",
			buf, pre_usbin_collapse);
	}
	else {
		pr_wa("Couldn't read USBIN_AICL_OPTIONS_CFG_REG\n");
		goto failed;
	}

	if (smblib_read(chg, AICL_RERUN_TIME_CFG_REG /*0x1661*/, &buf) >= 0) {
		pre_aicl_rerun_time = buf & AICL_RERUN_TIME_MASK;
		pr_wa("AICL_RERUN_TIME_CFG_REG=0x%02x, AICL_RERUN_TIME_MASK=0x%02x\n",
			buf, pre_aicl_rerun_time);
	}
	else {
		pr_wa("Couldn't read AICL_RERUN_TIME_CFG_REG\n");
		goto failed;
	}

// 4. Set 0s to AICL configurationss
	pr_wa("Setting USBIN_AICL_OPTIONS(0x1380[7]) := 0x00\n");
	if (smblib_masked_write(chg, USBIN_AICL_OPTIONS_CFG_REG, SUSPEND_ON_COLLAPSE_USBIN_BIT, 0) < 0) {
		pr_wa("Couldn't write USBIN_AICL_OPTIONS_CFG_REG\n");
		goto failed;
	}

	pr_wa("Setting AICL_RERUN_TIME_CFG_REG(0x1661[1:0]) := 0x00\n");
	if (smblib_masked_write(chg, AICL_RERUN_TIME_CFG_REG, AICL_RERUN_TIME_MASK, 0) < 0) {
		pr_wa("Couldn't write AICL_RERUN_TIME_CFG_REG\n");
		goto failed;
	}

// 5. Marginal delaying for AICL rerun
	pr_wa("Waiting more 3 secs . . .\n");
	msleep(AICL_RERUN_DELAY_MS);

// 6. Restore AICL configurations
	pr_wa("Restoring USBIN_AICL_OPTIONS(0x1380[7]) := 0x%02x\n", pre_usbin_collapse);
	if (smblib_masked_write(chg, USBIN_AICL_OPTIONS_CFG_REG, SUSPEND_ON_COLLAPSE_USBIN_BIT,
			pre_usbin_collapse) < 0) {
		pr_wa("Couldn't write USBIN_AICL_OPTIONS_CFG_REG\n");
		goto failed;
	}

	pr_wa("Restoring AICL_RERUN_TIME_CFG_REG(0x1661[1:0]) := 0x%02x\n", pre_aicl_rerun_time);
	if (smblib_masked_write(chg, AICL_RERUN_TIME_CFG_REG, AICL_RERUN_TIME_MASK,
			pre_aicl_rerun_time) < 0) {
		pr_wa("Couldn't write AICL_RERUN_TIME_CFG_REG\n");
		goto failed;
	}

// 7. If USBIN suspend is not resumed even with rerunning AICL, recover it from APSD.
	msleep(APSD_RERUN_DELAY_MS);
	if (wa_resuming_suspended_usbin_required()) {
		pr_wa("Recover USBIN from APSD\n");
		wa_command_apsd(chg);
	}
	else
		pr_wa("Success resuming suspended usbin\n");

	return;
failed:
	pr_wa("Error on resuming suspended usbin\n");
}

static DECLARE_DELAYED_WORK(wa_resuming_suspended_usbin_dwork, wa_resuming_suspended_usbin_func);

void wa_resuming_suspended_usbin_trigger(struct smb_charger* chg) {

	if (!wa_resuming_suspended_usbin_required()) {
		pr_wa(" Exiting recovery for USBIN-suspend.\n");
		return;
	}

	// Considering burst aicl-fail IRQs, previous wa works will be removed,
	// to make this trigger routine handle the latest aicl-fail
	if (delayed_work_pending(&wa_resuming_suspended_usbin_dwork)) {
		pr_wa("Cancel the pending resuming work . . .\n");
		cancel_delayed_work(&wa_resuming_suspended_usbin_dwork);
	}

	schedule_delayed_work(&wa_resuming_suspended_usbin_dwork,
		round_jiffies_relative(msecs_to_jiffies(INITIAL_DELAY_MS)));
}

void wa_resuming_suspended_usbin_clear(void) {
	cancel_delayed_work(&wa_resuming_suspended_usbin_dwork);
}


////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Clear cc status with Rd-open charger.
////////////////////////////////////////////////////////////////////////////

void wa_clear_cc_status_on_sink_charger_trigger(struct smb_charger* chg) {
	union power_supply_propval pval;

	if (chg->typec_mode == POWER_SUPPLY_TYPEC_SINK ) {
		pr_wa("Set TYPEC_PR_DUAL for rd open charger \n");
		pval.intval = POWER_SUPPLY_TYPEC_PR_DUAL;
		smblib_set_prop_typec_power_role(chg, &pval);
	}
}


////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Clear DC-PLUGIN stuck status
////////////////////////////////////////////////////////////////////////////

#define INT_LATCHED_CLR_OFFSET	0x14
#define INT_LATCHED_STS_OFFSET	0x18
void wa_clear_dc_plugin_stuck_trigger(struct smb_charger* chg) {
	struct power_supply* wireless_psy = power_supply_get_by_name("wireless");
	union power_supply_propval value = { 0, };
	u8 status;

	if (wireless_psy) {
		int wireless_online = !power_supply_get_property(wireless_psy,
			POWER_SUPPLY_PROP_ONLINE, &value) ? value.intval : 0;
		int dc_present = !smblib_get_prop_dc_present(chg, &value) ?
			value.intval : 0;
		bool dc_plugin = !smblib_read(chg, DCIN_BASE + INT_RT_STS_OFFSET, &status) ?
			(bool)(status & DCIN_PLUGIN_RT_STS_BIT) : false;

		pr_wa("dc_plugin status = wlc(%d)/dc(%d)/INT(0x%02x)\n",
			wireless_online, dc_present, status);

		if (wireless_online && !dc_present && dc_plugin) {
			smblib_read(chg, DCIN_BASE + INT_LATCHED_STS_OFFSET, &status);
			pr_wa("dc_plugin stuck occurs!!! 0x%02x\n", status);
			smblib_masked_write(chg, DCIN_BASE + INT_LATCHED_CLR_OFFSET,
				DCIN_PLUGIN_RT_STS_BIT, 0);
		}

		power_supply_put(wireless_psy);
	}
}


////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Clear burst DC UV interrupt
////////////////////////////////////////////////////////////////////////////

#define WEAK_WLC_STORM_COUNT	20
#define STATUS_BOOT_MS		25000
static void wa_clear_status_boot_func(struct work_struct *unused) {
	char buf[2] = { 0, };
	int boot_completed = 0;

	if (unified_nodes_show("status_boot", buf)
		&& sscanf(buf, "%d", &boot_completed) && boot_completed == 0) {
		unified_nodes_store("status_boot", "1", 1);
	}
}
static DECLARE_DELAYED_WORK(wa_clear_status_boot_dwork, wa_clear_status_boot_func);

static void wa_clear_burst_dc_uv_func(struct work_struct *unused) {
	struct smb_charger* chg = wa_helper_chg();
	struct power_supply* wireless_psy = power_supply_get_by_name("wireless");
	struct smb_irq_data *data;
	struct storm_watch *wdata;
	union power_supply_propval value = { .intval = 0, };
	char buf[2] = { 0, };
	int boot_completed = 0;

	if (!wireless_psy) {
		pr_wa("'wireless_psy' is not ready\n");
		return;
	}

	if (unified_nodes_show("status_boot", buf)
		&& sscanf(buf, "%d", &boot_completed) && boot_completed == 0) {
		value.intval = 1;
		power_supply_set_property(wireless_psy,
			POWER_SUPPLY_PROP_INPUT_SUSPEND, &value);
		schedule_delayed_work(&wa_clear_status_boot_dwork,
			round_jiffies_relative(msecs_to_jiffies(STATUS_BOOT_MS)));
	} else {
		data = chg->irq_info[DCIN_UV_IRQ].irq_data;
		if (data) {
			wdata = &data->storm_data;
			if (wdata->max_storm_count != WEAK_WLC_STORM_COUNT) {
				update_storm_count(wdata, WEAK_WLC_STORM_COUNT);
			}
			value.intval = 1;
			power_supply_set_property(wireless_psy,
				POWER_SUPPLY_PROP_DEBUG_BATTERY, &value);
		}
	}
	power_supply_put(wireless_psy);
}
static DECLARE_WORK(wa_clear_burst_dc_uv_work, wa_clear_burst_dc_uv_func);

void wa_clear_burst_dc_uv_trigger(struct smb_charger* chg) {
	schedule_work(&wa_clear_burst_dc_uv_work);
}


////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Recovery vashdn during wireless charging
////////////////////////////////////////////////////////////////////////////

#define VASHDN_DELAY_MS		3000
#define DCIN_VASHDN_RT_STS	BIT(1)
static void wa_recovery_vashdn_wireless_func(struct work_struct *unused) {
	struct smb_charger* chg = wa_helper_chg();
	union power_supply_propval val = { 0, };
	u8 stat;

	if (chg) {
		int dc_present = !smblib_get_prop_dc_present(chg, &val) ? val.intval : 0;
		int dc_online = !smblib_get_prop_dc_online(chg, &val) ? val.intval : 0;
		bool dc_vashdn = !smblib_read(chg, DCIN_BASE + INT_RT_STS_OFFSET, &stat)
			? (stat & DCIN_VASHDN_RT_STS) : 0;
		bool dc_pause = !smblib_read(chg, BATTERY_CHARGER_STATUS_1_REG, &stat)
			? (stat & PAUSE_CHARGE) : 0;

		if (dc_present && !dc_online && dc_vashdn && dc_pause) {
			pr_wa("detection Vashdn wireless charging stop!\n");
			schedule_work(&wa_clear_burst_dc_uv_work);
		}
	}
}
static DECLARE_DELAYED_WORK(wa_recovery_vashdn_wireless_dwork, wa_recovery_vashdn_wireless_func);

void wa_recovery_vashdn_wireless_trigger(void) {
	schedule_delayed_work(&wa_recovery_vashdn_wireless_dwork,
		round_jiffies_relative(msecs_to_jiffies(VASHDN_DELAY_MS)));
}


////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Retry to enable vconn on vconn-oc
////////////////////////////////////////////////////////////////////////////

#define VCONN_MAX_ATTEMPTS	3
#define MAX_OC_FALLING_TRIES 10
static int  wa_vconn_attempts = 0;

static void wa_retry_vconn_enable_on_vconn_oc_func(struct work_struct *unused) {
	struct smb_charger* chg = wa_helper_chg();
	static DEFINE_MUTEX(wa_vconn_oc_lock);
	int rc, i;
	u8 stat;

	pr_wa("over-current detected on VCONN\n");
	if (!chg->vconn_vreg || !chg->vconn_vreg->rdev)
		return;

	mutex_lock(&wa_vconn_oc_lock);
	rc = override_vconn_regulator_disable(chg->vconn_vreg->rdev);
	if (rc < 0) {
		pr_wa("Couldn't disable VCONN rc=%d\n", rc);
		goto unlock;
	}

	if (++wa_vconn_attempts > VCONN_MAX_ATTEMPTS) {
		pr_wa("VCONN failed to enable after %d attempts\n",
			   wa_vconn_attempts - 1);
		wa_vconn_attempts = 0;
		goto unlock;
	}

	/*
	 * The real time status should go low within 10ms. Poll every 1-2ms to
	 * minimize the delay when re-enabling OTG.
	 */
	for (i = 0; i < MAX_OC_FALLING_TRIES; ++i) {
		usleep_range(1000, 2000);
		rc = smblib_read(chg, TYPE_C_MISC_STATUS_REG, &stat);
		if (rc >= 0 && !(stat & TYPEC_VCONN_OVERCURR_STATUS_BIT))
			break;
	}

	if (i >= MAX_OC_FALLING_TRIES) {
		pr_wa("VCONN OC did not fall after %dms\n",
						2 * MAX_OC_FALLING_TRIES);
		wa_vconn_attempts = 0;
		goto unlock;
	}
	pr_wa("VCONN OC fell after %dms\n", 2 * i + 1);

	rc = override_vconn_regulator_enable(chg->vconn_vreg->rdev);
	if (rc < 0) {
		pr_wa("Couldn't enable VCONN rc=%d\n", rc);
		goto unlock;
	}

unlock:
	mutex_unlock(&wa_vconn_oc_lock);
}
static DECLARE_WORK(wa_retry_vconn_enable_on_vconn_oc_work, wa_retry_vconn_enable_on_vconn_oc_func);

void wa_retry_vconn_enable_on_vconn_oc_trigger(struct smb_charger* chg) {
	u8 stat;
	int rc;

	rc = smblib_read(chg, TYPE_C_MISC_STATUS_REG, &stat);
	if (rc < 0) {
		pr_wa("Couldn't read TYPE_C_MISC_STATUS_REG rc=%d\n", rc);
		return;
	}

	if (stat & TYPEC_VCONN_OVERCURR_STATUS_BIT)
		schedule_work(&wa_retry_vconn_enable_on_vconn_oc_work);
}

void wa_retry_vconn_enable_on_vconn_oc_clear(struct smb_charger* chg) {
	int rc;

	if (!chg->vconn_vreg->rdev)
		return;

	if(smblib_vconn_regulator_is_enabled(chg->vconn_vreg->rdev)
			&& chg->typec_mode == POWER_SUPPLY_TYPEC_NONE
			&& wa_vconn_attempts != 0) {
		rc = override_vconn_regulator_disable(chg->vconn_vreg->rdev);
		if (rc < 0) {
			pr_wa("Couldn't disable VCONN rc=%d\n", rc);
			return;
		}
	}
	wa_vconn_attempts = 0;
}


////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Supporting USB Compliance test
////////////////////////////////////////////////////////////////////////////

static bool wa_usb_compliance_mode = false;
#define MICRO_2P5A			2500000

bool wa_is_usb_compliance_mode(void) {
	return wa_usb_compliance_mode;
}

void wa_set_usb_compliance_mode(bool mode) {
	struct smb_charger* chg = wa_helper_chg();
	int rc;

	if (!chg) {
		pr_wa("'chg' is not ready\n");
		return;
	}

	wa_usb_compliance_mode = mode;
	if (wa_usb_compliance_mode) {
		/* set OTG current limit */
		rc = smblib_set_charge_param(chg, &chg->param.otg_cl, MICRO_2P5A);
		if (rc < 0) {
			pr_err("Couldn't set otg current limit rc=%d on compliance mode\n", rc);
			return;
		}

		/* Check for VBUS at vSAFE0V before transitioning into ATTACHED.SRC state */
		rc = smblib_masked_write(chg, TYPE_C_EXIT_STATE_CFG_REG,
			BYPASS_VSAFE0V_DURING_ROLE_SWAP_BIT, 0);
		if (rc < 0) {
			pr_err("Couldn't set exit state cfg rc=%d\n", rc);
			return;
		}

		/* disable crude sensors */
		rc = smblib_masked_write(chg, TYPE_C_CRUDE_SENSOR_CFG_REG,
				EN_SRC_CRUDE_SENSOR_BIT |
				EN_SNK_CRUDE_SENSOR_BIT,
				0);
		if (rc < 0) {
			pr_err("Couldn't disable crude sensor rc=%d\n", rc);
			return;
		}

		/*
		 * Do not apply tPDdebounce on exit condition for
		 * attachedwait.SRC and attached.SRC
		 */
		rc = smblib_masked_write(chg, TYPE_C_EXIT_STATE_CFG_REG,
				BIT(1), 0);
		if (rc < 0) {
			pr_err("Couldn't set USE_TPD_FOR_EXITING_ATTACHSRC rc=%d\n", rc);
			return;
		}
	} else {
		/* set OTG current limit */
		rc = smblib_set_charge_param(chg, &chg->param.otg_cl, chg->otg_cl_ua);
		if (rc < 0) {
			pr_err("Couldn't set otg current limit rc=%d on non-compliance mode\n", rc);
			return;
		}
	}
}


////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Floating type detected over rerunning APSD
////////////////////////////////////////////////////////////////////////////

static atomic_t wa_fdr_running = ATOMIC_INIT(0);
static atomic_t wa_fdr_ready = ATOMIC_INIT(0);

static void wa_floating_during_rerun_work(struct work_struct *unused) {
// 1. Getting swa_floating_during_rerun_apsdmb_charger
	struct smb_charger* chg = wa_helper_chg();
	if (!chg) {
		pr_info("[W/A] FDR) charger driver is not ready\n");
		return;
	}

// 2. Rerunning APSD
	wa_command_apsd(chg);

// 3. Resetting this running flag
	atomic_set(&wa_fdr_running, 0);
}
static DECLARE_WORK(wa_fdr_dwork, wa_floating_during_rerun_work);

void wa_floating_during_rerun_apsd(enum power_supply_type pst) {
	bool charger_unknown = (pst == POWER_SUPPLY_TYPE_UNKNOWN);
	bool charger_float = (pst == POWER_SUPPLY_TYPE_USB_FLOAT);
	bool fdr_running = wa_floating_during_rerun_working();
	bool fdr_ready = !!atomic_read(&wa_fdr_ready);
	pr_info("[W/A] FDR) pstype=%d, running=%d, ready=%d\n",
		pst, fdr_running, fdr_ready);

	if (!charger_unknown && !charger_float && !fdr_ready) {
		pr_info("[W/A] FDR) Being ready to check runtime floating\n");
		atomic_set(&wa_fdr_ready, 1);
		return;
	}

	if (fdr_ready && !fdr_running && charger_float) {
		pr_info("[W/A] FDR) Rerunning APSD\n");
		atomic_set(&wa_fdr_running, 1);
		schedule_work(&wa_fdr_dwork);
	}

}

void wa_floating_during_rerun_clear(void) {
	atomic_set(&wa_fdr_ready, 0);
}

bool wa_floating_during_rerun_working(void) {
	return !!atomic_read(&wa_fdr_running);
}
