// SPDX-License-Identifier: GPL-2.0-only
/*
 * MacBookPro14,2: wake from s2idle when the lid opens.
 *
 * The lid reports only through the embedded controller: EC query _Q20 does
 * Notify(LID0, 0x80) on the EC GPE (EC._GPE = 0x07). LID0's own wake GPE 0x6F
 * never fires under Linux. Linux arms the EC GPE for suspend-to-idle only in
 * lps0_device_attach() (drivers/acpi/x86/s2idle.c), and this machine has no
 * LPS0 device. So the EC GPE is off during s2idle, opening the lid does not
 * reach the CPU, and only a key press (SPI GPE) wakes the system (21.11.2026).
 *
 * This module does what the kernel does on LPS0 systems: it marks the EC GPE
 * as wake-capable and sets its wake mask, only for suspend-to-idle
 * (PM_SUSPEND_PREPARE .. PM_POST_SUSPEND, only when s2idle is the default).
 * acpi_ec_dispatch_gpe() then runs EC events during s2idle. The lid notify
 * calls acpi_pm_wakeup_event() and resumes the system. Other EC events
 * (battery, AC) are handled and the system goes back to sleep.
 * Hibernation and power-off never see the wake mask.
 *
 * Parameters: enabled (0/1, live), armed_cycles (read-only), gpe (read-only).
 */
#define pr_fmt(fmt) "mbp142-lid-wake: " fmt

#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/suspend.h>

static bool enabled = true;
module_param(enabled, bool, 0644);
MODULE_PARM_DESC(enabled, "Arm the EC GPE for lid wake during s2idle");

static int gpe = -1;
module_param(gpe, int, 0444);
static unsigned int armed_cycles;
module_param(armed_cycles, uint, 0444);

static bool armed;

static int lid_wake_pm_notify(struct notifier_block *nb, unsigned long action,
			      void *data)
{
	acpi_status status;

	switch (action) {
	case PM_SUSPEND_PREPARE:
		if (!enabled || !pm_suspend_default_s2idle())
			break;
		status = acpi_set_gpe_wake_mask(NULL, gpe, ACPI_GPE_ENABLE);
		if (ACPI_FAILURE(status)) {
			pr_warn("enabling wake mask for GPE 0x%02x failed: %s\n",
				gpe, acpi_format_exception(status));
			break;
		}
		armed = true;
		armed_cycles++;
		pr_info("EC GPE 0x%02x armed for s2idle wake (cycle %u)\n",
			gpe, armed_cycles);
		break;
	case PM_POST_SUSPEND:
		if (!armed)
			break;
		acpi_set_gpe_wake_mask(NULL, gpe, ACPI_GPE_DISABLE);
		armed = false;
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block lid_wake_nb = {
	.notifier_call = lid_wake_pm_notify,
};

static int __init lid_wake_init(void)
{
	unsigned long long value;
	acpi_handle ec;
	acpi_status status;
	int ret;

	if (!dmi_match(DMI_SYS_VENDOR, "Apple Inc.") ||
	    !dmi_match(DMI_PRODUCT_NAME, "MacBookPro14,2"))
		return -ENODEV;

	status = acpi_get_handle(NULL, "\\_SB.PCI0.LPCB.EC", &ec);
	if (ACPI_FAILURE(status))
		return -ENODEV;
	status = acpi_evaluate_integer(ec, "_GPE", NULL, &value);
	if (ACPI_FAILURE(status) || value > 0xff) {
		pr_err("cannot read EC _GPE\n");
		return -ENODEV;
	}
	gpe = value;

	status = acpi_mark_gpe_for_wake(NULL, gpe);
	if (ACPI_FAILURE(status)) {
		pr_err("marking GPE 0x%02x for wake failed: %s\n", gpe,
		       acpi_format_exception(status));
		return -EIO;
	}

	ret = register_pm_notifier(&lid_wake_nb);
	if (ret)
		return ret;
	pr_info("loaded, EC GPE 0x%02x, s2idle default %d, enabled %d\n", gpe,
		pm_suspend_default_s2idle(), enabled);
	return 0;
}

static void __exit lid_wake_exit(void)
{
	unregister_pm_notifier(&lid_wake_nb);
	if (armed)
		acpi_set_gpe_wake_mask(NULL, gpe, ACPI_GPE_DISABLE);
	pr_info("unloaded\n");
}

module_init(lid_wake_init);
module_exit(lid_wake_exit);
MODULE_LICENSE("GPL");
MODULE_VERSION("1");
MODULE_DESCRIPTION("MacBookPro14,2: wake from s2idle on lid open (EC GPE)");
