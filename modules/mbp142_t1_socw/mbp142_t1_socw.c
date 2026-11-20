// SPDX-License-Identifier: GPL-2.0-only
/*
 * MacBookPro14,2: Apple's host sleep/wake notification to the T1.
 *
 * \_SB.PCI0.XHC1.RHUB.ASOC.SOCW(0|1) sends EC mailbox commands to the T1
 * (SOCW(0) = MBOX(1), SOCW(1) = MBOX(6) then MBOX(2)). macOS sends them around
 * every sleep. Linux runs _PTS/_WAK for S3/S4 (EC.ECSS) but never SOCW, so after
 * hibernation the T1 keeps Touch Bar and Touch ID off (20.11.2026: dark panel,
 * restored by SOCW(1) followed by a restart of the T1Bridge services).
 *
 * Nothing happens on load. Write 0 or 1 to parameters/socw; the AML return value
 * (0 = success) is kept in socw_result.
 */
#define pr_fmt(fmt) "mbp142-t1-socw: " fmt

#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/mutex.h>

static acpi_handle asoc;
static DEFINE_MUTEX(socw_lock);
static int socw_result = -1;
module_param(socw_result, int, 0444);

static int socw_set(const char *value, const struct kernel_param *kp)
{
	union acpi_object arg = { .integer = { .type = ACPI_TYPE_INTEGER } };
	struct acpi_object_list args = { .count = 1, .pointer = &arg };
	unsigned long long result = ~0ULL;
	acpi_status status;
	unsigned int which;
	int ret = kstrtouint(value, 0, &which);

	if (ret)
		return ret;
	if (which > 1)
		return -EINVAL;
	arg.integer.value = which;
	mutex_lock(&socw_lock);
	status = acpi_evaluate_integer(asoc, "SOCW", &args, &result);
	mutex_unlock(&socw_lock);
	if (ACPI_FAILURE(status)) {
		pr_err("SOCW(%u) failed: %s\n", which, acpi_format_exception(status));
		return -EIO;
	}
	socw_result = (int)result;
	pr_info("SOCW(%u) returned %llu\n", which, result);
	return 0;
}

static int socw_get(char *buf, const struct kernel_param *kp)
{
	return sysfs_emit(buf, "-\n");
}

static const struct kernel_param_ops socw_ops = { .set = socw_set, .get = socw_get };
module_param_cb(socw, &socw_ops, NULL, 0200);

static int __init t1_socw_init(void)
{
	if (!dmi_match(DMI_SYS_VENDOR, "Apple Inc.") ||
	    !dmi_match(DMI_PRODUCT_NAME, "MacBookPro14,2"))
		return -ENODEV;
	if (ACPI_FAILURE(acpi_get_handle(NULL, "\\_SB_.PCI0.XHC1.RHUB.ASOC", &asoc)) ||
	    !acpi_has_method(asoc, "SOCW"))
		return -ENODEV;
	return 0;
}

static void __exit t1_socw_exit(void)
{
}

module_init(t1_socw_init);
module_exit(t1_socw_exit);
MODULE_LICENSE("GPL");
MODULE_VERSION("1");
MODULE_DESCRIPTION("MacBookPro14,2: Apple T1 host sleep/wake notification (ASOC.SOCW)");
