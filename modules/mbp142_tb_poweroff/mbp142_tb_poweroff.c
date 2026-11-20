// SPDX-License-Identifier: GPL-2.0-only
#define pr_fmt(fmt) "mbp142-tb-poweroff: " fmt
/*
 * MacBookPro14,2: power both idle Alpine Ridge Thunderbolt controllers off
 * during s2idle, the way macOS does when nothing is attached.
 * On every s2idle entry with no external USB/Thunderbolt device, both Alpine
 * Ridge controllers go through Go2Sx, Link Disable and Apple's SXFP/XRST
 * sequence and are restored at wake. A failed check skips the power-down and
 * lets the system sleep normally. A failed recovery disables further attempts.
 *
 * Optional awake mode (parameter "awake": RP05, RP09 or on) powers a single idle
 * side off while the system runs; macbook-tb-awake drives it. Before any system
 * sleep, both sides are powered on again so the sleep path starts from a known
 * state.
 */

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/fs.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/power_supply.h>
#include <linux/suspend.h>
#include <linux/timekeeping.h>
#include <linux/usb.h>

enum branch_node { ROOT, UPSB, DSB0, DSB1, DSB2, DSB4, NHI, XHC, NODE_COUNT };

struct root_link_state {
	u16 control;
	u16 status;
	u16 pmcsr;
	u8 l23;
};

struct retained_device {
	u32 header[PCI_STD_HEADER_SIZEOF / sizeof(u32)];
	struct {
		resource_size_t start, end;
		unsigned long flags;
	} resource[PCI_NUM_RESOURCES];
	bool override, usage, disabled, isolated, saved;
};

struct guarded_bus {
	struct pci_bus *bus;
	struct pci_ops *original;
};

struct branch {
	const char *name;
	const char *usb_name;
	u8 root_devfn;
	u16 root_id;
	u32 nhi_pad, usb_pad, reset_pad;
	struct pci_dev *devices[NODE_COUNT];
	acpi_handle root_handle, nhi_handle, dsb_handle, xhc_handle;
	struct retained_device retained[NODE_COUNT];
	struct guarded_bus buses[NHI];
	u32 route[NHI];
	u16 vsec;
	bool link_touched;
	bool link_was_disabled;
	bool held_once;
	bool awake_off, wake_pending, notify_installed;
	bool attached, extra, pinned, fenced, request, pads, reset, verified;
};

static struct branch branches[] = {
	{ .name = "RP05", .usb_name = "XHC2", .root_devfn = PCI_DEVFN(0x1c, 4),
	  .root_id = 0x9d14, .nhi_pad = 0x02060000, .usb_pad = 0x02060001,
	  .reset_pad = 0x02060004 },
	{ .name = "RP09", .usb_name = "XHC3", .root_devfn = PCI_DEVFN(0x1d, 0),
	  .root_id = 0x9d18, .nhi_pad = 0x02060002, .usb_pad = 0x02060003,
	  .reset_pad = 0x02060005 },
};

static const struct dev_pm_ops *native_pm;
static struct dev_pm_domain domain;
/* A non-NULL domain with no callbacks suppresses the PCI bus PM callbacks.
 * All devices using it are also driverless and have runtime PM disabled.
 */
static struct dev_pm_domain isolated_domain;
static DEFINE_MUTEX(transition_lock);
static unsigned int suspended_roots;
static unsigned int resumed_roots;
static bool enabled = true;
static bool skip_power;
static unsigned int cycles, powered_off, skipped;
static bool finished;
static bool trial;
static bool pair_powered_off;
static ktime_t before_sleep;
static unsigned long long sleep_ns;
static unsigned long long wake_mono_ns;
static bool succeeded;
static bool degraded;
module_param(enabled, bool, 0644);
MODULE_PARM_DESC(enabled, "Power both idle Thunderbolt controllers off during s2idle (default Y)");
module_param(cycles, uint, 0444);
module_param(powered_off, uint, 0444);
module_param(skipped, uint, 0444);
module_param(finished, bool, 0444);
module_param(succeeded, bool, 0444);
module_param(degraded, bool, 0444);
module_param(sleep_ns, ullong, 0444);
module_param(wake_mono_ns, ullong, 0444);

static int fence_read(struct pci_bus *bus, unsigned int devfn, int pos,
		      int size, u32 *value)
{
	*value = U32_MAX;
	return PCIBIOS_DEVICE_NOT_FOUND;
}

static int fence_write(struct pci_bus *bus, unsigned int devfn, int pos,
		       int size, u32 value)
{
	return PCIBIOS_DEVICE_NOT_FOUND;
}

static struct pci_ops fenced_ops = { .read = fence_read, .write = fence_write };

/*
 * The PCI devices and their resource reservations stay registered. The
 * original x86 bus operations are used only by the verified recovery path.
 */
static int config_access(struct branch *branch, enum branch_node node, int reg,
			 int width, u32 *value, bool write)
{
	struct pci_dev *pdev = branch->devices[node];
	int i;

	if (branch->fenced && node != ROOT) {
		for (i = ROOT; i < NHI; i++) {
			struct guarded_bus *entry = &branch->buses[i];

			if (entry->bus != pdev->bus)
				continue;
			if (write)
				return entry->original->write(entry->bus, pdev->devfn,
							      reg, width, *value);
			return entry->original->read(entry->bus, pdev->devfn,
						     reg, width, value);
		}
		return PCIBIOS_DEVICE_NOT_FOUND;
	}
	if (width == 2) {
		u16 word = U16_MAX;
		int ret;

		if (write)
			return pci_write_config_word(pdev, reg, *value);
		ret = pci_read_config_word(pdev, reg, &word);
		*value = word;
		return ret;
	}
	if (write)
		return pci_write_config_dword(pdev, reg, *value);
	return pci_read_config_dword(pdev, reg, value);
}

static int config_read(struct branch *branch, enum branch_node node, int reg, u32 *value)
{
	return config_access(branch, node, reg, 4, value, false);
}

static int config_write(struct branch *branch, enum branch_node node, int reg, u32 value)
{
	return config_access(branch, node, reg, 4, &value, true);
}

static int identity(struct branch *branch, enum branch_node node)
{
	struct pci_dev *pdev = branch->devices[node];
	u32 id;

	if (config_read(branch, node, PCI_VENDOR_ID, &id) ||
	    id != ((u32)pdev->device << 16 | pdev->vendor))
		return -ENODEV;
	return 0;
}

static int bridge_route(struct branch *branch, enum branch_node node, bool save, bool repair)
{
	u32 buses;

	if (identity(branch, node) ||
	    config_read(branch, node, PCI_PRIMARY_BUS, &buses) || buses == U32_MAX)
		return -ENODEV;
	if (save) {
		branch->route[node] = buses;
		return 0;
	}
	if ((buses & GENMASK(23, 0)) == (branch->route[node] & GENMASK(23, 0)))
		return 0;
	if (!repair || config_write(branch, node, PCI_PRIMARY_BUS, branch->route[node]) ||
	    config_read(branch, node, PCI_PRIMARY_BUS, &buses) ||
	    buses != branch->route[node])
		return -EIO;
	return 0;
}

static int save_addresses(struct branch *branch, enum branch_node node)
{
	struct pci_dev *pdev = branch->devices[node];
	struct retained_device *saved = &branch->retained[node];
	int i;

	if (identity(branch, node))
		return -ENODEV;
	for (i = 0; i < ARRAY_SIZE(saved->header); i++)
		if (config_read(branch, node, i * 4, &saved->header[i]))
			return -EIO;
	for (i = 0; i < ARRAY_SIZE(saved->resource); i++) {
		saved->resource[i].start = pdev->resource[i].start;
		saved->resource[i].end = pdev->resource[i].end;
		saved->resource[i].flags = pdev->resource[i].flags;
	}
	return 0;
}

static int preserve_addresses(struct branch *branch, enum branch_node node, bool repair)
{
	static const int bridge_regs[] = {
		PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_1, PCI_IO_BASE,
		PCI_MEMORY_BASE, PCI_PREF_MEMORY_BASE, PCI_PREF_BASE_UPPER32,
		PCI_PREF_LIMIT_UPPER32, PCI_IO_BASE_UPPER16,
	};
	static const int endpoint_regs[] = {
		PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_1, PCI_BASE_ADDRESS_2,
		PCI_BASE_ADDRESS_3, PCI_BASE_ADDRESS_4, PCI_BASE_ADDRESS_5,
	};
	const int *regs = node < NHI ? bridge_regs : endpoint_regs;
	int count = node < NHI ? ARRAY_SIZE(bridge_regs) : ARRAY_SIZE(endpoint_regs);
	struct retained_device *saved = &branch->retained[node];
	struct pci_dev *pdev = branch->devices[node];
	u32 actual, expected;
	int i, reg, width;

	if (identity(branch, node))
		return -ENODEV;
	for (i = 0; i < ARRAY_SIZE(saved->resource); i++)
		if (saved->resource[i].start != pdev->resource[i].start ||
		    saved->resource[i].end != pdev->resource[i].end ||
		    saved->resource[i].flags != pdev->resource[i].flags)
			return -EXDEV;
	/* Write the high half of 64-bit BARs before the low half. */
	for (i = count - 1; i >= 0; i--) {
		reg = regs[i];
		width = node < NHI && reg == PCI_IO_BASE ? 2 : 4;
		expected = saved->header[reg / 4];
		if (width == 2)
			expected &= U16_MAX;
		if (config_access(branch, node, reg, width, &actual, false))
			return -EIO;
		if (actual == expected)
			continue;
		if (!repair ||
		    config_access(branch, node, reg, width, &expected, true) ||
		    config_access(branch, node, reg, width, &actual, false) ||
		    actual != expected)
			return -EIO;
		pr_info("%s %s restored config %02x=%08x\n",
			branch->name, pci_name(pdev), reg, expected);
	}
	return 0;
}

static int aml(acpi_handle handle, const char *method, unsigned int count,
	       u32 first, u32 second, unsigned long long *value)
{
	union acpi_object args[2] = {
		{ .integer = { .type = ACPI_TYPE_INTEGER, .value = first } },
		{ .integer = { .type = ACPI_TYPE_INTEGER, .value = second } },
	};
	struct acpi_object_list arguments = { .count = count, .pointer = args };
	acpi_status status;

	if (value)
		status = acpi_evaluate_integer(handle, (char *)method, &arguments, value);
	else
		status = acpi_evaluate_object(handle, (char *)method, &arguments, NULL);
	if (ACPI_FAILURE(status)) {
		pr_warn("%s failed: %s\n", method, acpi_format_exception(status));
		return -EIO;
	}
	return 0;
}

static int pad_low(u32 pad, bool *low)
{
	unsigned long long direction, output;

	if (aml(NULL, "\\_SB_.GGDV", 1, pad, 0, &direction) ||
	    aml(NULL, "\\_SB_.GGOV", 1, pad, 0, &output))
		return -EIO;
	*low = !direction && !output;
	return 0;
}

static int pad_input(u32 pad)
{
	unsigned long long direction;

	return aml(NULL, "\\_SB_.GGDV", 1, pad, 0, &direction) ||
	       direction != 1 ? -EAGAIN : 0;
}

static int conditions(void)
{
	unsigned long long osdw, sltp;

	if (aml(NULL, "\\OSDW", 0, 0, 0, &osdw) ||
	    aml(NULL, "\\SLTP", 0, 0, 0, &sltp))
		return -EIO;
	return osdw == 1 && sltp == 0 ? 0 : -EAGAIN;
}

static int verify_online(struct branch *branch)
{
	enum branch_node i;

	for (i = ROOT; i < NHI; i++)
		if (bridge_route(branch, i, false, false))
			return -ENODEV;
	return identity(branch, NHI) ?: identity(branch, XHC);
}

static int verify_addresses(struct branch *branch)
{
	enum branch_node node;

	if (verify_online(branch))
		return -ENODEV;
	for (node = ROOT; node < NODE_COUNT; node++)
		if (preserve_addresses(branch, node, false))
			return -EIO;
	return 0;
}

static int begin_request(struct branch *branch)
{
	u32 reply, request;

	if (verify_online(branch) ||
	    config_read(branch, UPSB, branch->vsec + 0x48, &reply) ||
	    config_read(branch, UPSB, branch->vsec + 0x4c, &request))
		return -EIO;
	if (reply == U32_MAX || (reply & 0xff) == 5 || request)
		return -EBUSY;
	branch->request = true;
	return config_write(branch, UPSB, branch->vsec + 0x4c, 5) ? -EIO : 0;
}

static int verify_ack(struct branch *branch)
{
	u32 reply = U32_MAX;
	int ret, read_ret;

	ret = read_poll_timeout(config_read, read_ret, read_ret || reply == U32_MAX ||
				(reply & 0xff) == 5, 1000, 650000, false,
				branch, UPSB, branch->vsec + 0x48, &reply);
	pr_info("%s Go2Sx acknowledgement %#010x (poll %d)\n",
		branch->name, reply, ret);
	return ret ?: (read_ret || reply == U32_MAX || (reply & 0xff) != 5 ? -EIO : 0);
}

static int clear_request(struct branch *branch)
{
	u32 request;

	if (!branch->request)
		return 0;
	if (identity(branch, UPSB) ||
	    config_write(branch, UPSB, branch->vsec + 0x4c, 0) ||
	    config_read(branch, UPSB, branch->vsec + 0x4c, &request) || request)
		return -EIO;
	branch->request = false;
	return 0;
}

static int read_root_link(struct branch *branch, struct root_link_state *link)
{
	struct pci_dev *root = branch->devices[ROOT];

	/* TbtOnPCH calls the two bits in this last byte L23E and L23D. */
	if (!root->pm_cap ||
	    pci_read_config_word(root, root->pm_cap + PCI_PM_CTRL, &link->pmcsr) ||
	    pcie_capability_read_word(root, PCI_EXP_LNKCTL, &link->control) ||
	    pcie_capability_read_word(root, PCI_EXP_LNKSTA, &link->status) ||
	    pci_read_config_byte(root, 0xe2, &link->l23) ||
	    link->pmcsr == U16_MAX || link->control == U16_MAX || link->status == U16_MAX)
		return -EIO;
	return 0;
}

static bool link_settled(const struct root_link_state *link, bool disabled)
{
	return !!(link->control & PCI_EXP_LNKCTL_LD) == disabled &&
	       !!(link->status & PCI_EXP_LNKSTA_DLLLA) == !disabled &&
	       !(link->status & PCI_EXP_LNKSTA_LT);
}

static void log_root_link(struct branch *branch, const char *phase,
			  const struct root_link_state *link, int ret)
{
	pr_info("%s %s: LNKCTL=%#06x LNKSTA=%#06x PMCSR=%#06x L23=%#04x result=%d\n",
		branch->name, phase, link->control, link->status, link->pmcsr,
		link->l23, ret);
}

static int verify_link_start(struct branch *branch)
{
	struct pci_dev *root = branch->devices[ROOT];
	struct root_link_state link = {};
	u32 cap;
	int ret;

	if (bridge_route(branch, ROOT, false, false) ||
	    pcie_capability_read_dword(root, PCI_EXP_LNKCAP, &cap) ||
	    cap == U32_MAX || !(cap & PCI_EXP_LNKCAP_DLLLARC))
		return -ENODEV;
	ret = read_root_link(branch, &link);
	if (!ret && ((link.pmcsr & PCI_PM_CTRL_STATE_MASK) != PCI_D0 ||
		     !link_settled(&link, false)))
		ret = -EAGAIN;
	if (!ret)
		branch->link_was_disabled = !!(link.control & PCI_EXP_LNKCTL_LD);
	log_root_link(branch, "before Link Disable", &link, ret);
	return ret;
}

static int disable_link(struct branch *branch)
{
	/* A failed config operation can still have reached the Root Port. */
	branch->link_touched = true;
	return pcibios_err_to_errno(pcie_capability_set_word(branch->devices[ROOT],
						       PCI_EXP_LNKCTL, PCI_EXP_LNKCTL_LD));
}

static int wait_root_link(struct branch *branch, bool disabled, u16 state,
			  unsigned long timeout_us, const char *phase)
{
	struct root_link_state link = {};
	int ret, read_ret;

	if (timeout_us) {
		ret = read_poll_timeout(read_root_link, read_ret,
					read_ret || ((link.pmcsr & PCI_PM_CTRL_STATE_MASK) == state &&
						     link_settled(&link, disabled)),
					2000, timeout_us, false, branch, &link);
		ret = ret ?: read_ret;
	} else {
		ret = read_root_link(branch, &link);
		if (!ret && ((link.pmcsr & PCI_PM_CTRL_STATE_MASK) != state ||
			     !link_settled(&link, disabled)))
			ret = -EAGAIN;
	}
	log_root_link(branch, phase, &link, ret);
	return ret;
}

static int verify_off(struct branch *branch, const char *phase, bool wait)
{
	struct root_link_state link = {};
	u32 id = 0;
	bool nhi, usb, reset;
	int ret;

	if (bridge_route(branch, ROOT, false, false))
		return -EIO;
	if (wait)
		ret = wait_root_link(branch, true, PCI_D3hot, 500000, phase);
	else {
		ret = read_root_link(branch, &link);
		if (!ret && ((link.pmcsr & PCI_PM_CTRL_STATE_MASK) != PCI_D3hot ||
			     !link_settled(&link, true)))
			ret = -EIO;
		log_root_link(branch, phase, &link, ret);
	}
	if (ret)
		return ret;
	/* Sample the child once, after the validated Root Port says link down. */
	if (config_read(branch, UPSB, PCI_VENDOR_ID, &id))
		return -EIO;
	if (pad_low(branch->nhi_pad, &nhi) || pad_low(branch->usb_pad, &usb) ||
	    pad_low(branch->reset_pad, &reset))
		return -EIO;
	pr_info("%s %s: UPSB %#010x, output-low NHI=%d USB=%d RESET=%d\n",
		branch->name, phase, id, nhi, usb, reset);
	return id == U32_MAX && nhi && usb && reset ? 0 : -EIO;
}

static int power_off(struct branch *branch)
{
	branch->pads = true;
	if (aml(branch->nhi_handle, "SXFP", 1, 0, 0, NULL))
		return -EIO;
	branch->reset = true;
	if (aml(branch->nhi_handle, "XRST", 1, 1, 0, NULL))
		return -EIO;
	return verify_off(branch, "after power-down", true);
}

static int verify_released(struct branch *branch)
{
	unsigned long long reset, port_state, link_disabled;

	if (aml(branch->nhi_handle, "XRTE", 0, 0, 0, &reset) ||
	    aml(branch->root_handle, "PSTA", 0, 0, 0, &port_state) ||
	    aml(branch->root_handle, "LDIS", 0, 0, 0, &link_disabled) ||
	    pad_input(branch->nhi_pad) || pad_input(branch->usb_pad) ||
	    pad_input(branch->reset_pad))
		return -EIO;
	return !reset && !port_state && !link_disabled ? 0 : -EIO;
}

static int recover_root(struct branch *branch)
{
	struct pci_dev *root = branch->devices[ROOT];
	struct root_link_state link = {};
	unsigned long long reset;
	int ret;

	if (!branch->link_touched && !branch->reset && !branch->pads)
		return 0;
	if (identity(branch, ROOT))
		return -ENODEV;
	/* PCEU releases the firmware reset and returns the port to D0. */
	if (branch->reset || branch->pads) {
		aml(branch->root_handle, "_PS0", 0, 0, 0, NULL);
		if (aml(branch->nhi_handle, "XRTE", 0, 0, 0, &reset) || reset)
			aml(branch->nhi_handle, "XRST", 1, 0, 0, NULL);
	}
	ret = read_root_link(branch, &link);
	if (!ret && (link.pmcsr & PCI_PM_CTRL_STATE_MASK) != PCI_D0)
		ret = -EIO;
	if (!ret && branch->link_touched &&
	    !!(link.control & PCI_EXP_LNKCTL_LD) != branch->link_was_disabled) {
		ret = pcibios_err_to_errno(pcie_capability_clear_and_set_word(root, PCI_EXP_LNKCTL,
					PCI_EXP_LNKCTL_LD,
					branch->link_was_disabled ? PCI_EXP_LNKCTL_LD : 0));
		if (!ret)
			ret = read_root_link(branch, &link);
	}
	if (!ret && (link.pmcsr & PCI_PM_CTRL_STATE_MASK) != PCI_D0)
		ret = -EIO;
	if (!ret && branch->link_touched &&
	    !!(link.control & PCI_EXP_LNKCTL_LD) != branch->link_was_disabled)
		ret = -EIO;
	log_root_link(branch, "original Link Disable restored", &link, ret);
	return ret;
}

static int recover_branch(struct branch *branch)
{
	enum branch_node i;
	int ret;

	ret = bridge_route(branch, ROOT, false, true);
	if (ret)
		return ret;
	ret = preserve_addresses(branch, ROOT, false);
	if (ret)
		return ret;
	if (branch->reset || branch->pads)
		aml(branch->nhi_handle, "TRPE", 2, 1, 0, NULL);
	if (branch->link_touched) {
		ret = wait_root_link(branch, false, PCI_D0, 1000000, "link active after recovery");
		if (ret)
			return ret;
		/* PCIe allows endpoint readiness up to 100 ms after link training. */
		msleep(100);
	}
	ret = read_poll_timeout(identity, ret, !ret, 2000, 400000, false,
				branch, UPSB);
	if (ret)
		return ret;
	if (clear_request(branch))
		return -EIO;
	for (i = UPSB; i < NHI; i++) {
		ret = bridge_route(branch, i, false, true);
		if (ret)
			return ret;
		ret = preserve_addresses(branch, i, true);
		if (ret)
			return ret;
	}
	if (branch->pads &&
	    (aml(branch->dsb_handle, "_PS0", 0, 0, 0, NULL) ||
	     aml(branch->xhc_handle, "_PS0", 0, 0, 0, NULL) ||
	     verify_released(branch)))
		return -EIO;
	for (i = NHI; i < NODE_COUNT; i++) {
		ret = preserve_addresses(branch, i, true);
		if (ret)
			return ret;
	}
	ret = verify_addresses(branch);
	if (!ret) {
		branch->pads = false;
		branch->reset = false;
		branch->link_touched = false;
	}
	return ret;
}

static void recover_pair(const char *phase)
{
	int root_result[ARRAY_SIZE(branches)] = {};
	int i, ret;

	/* Undo both host-port changes before waiting for either device tree. */
	for (i = ARRAY_SIZE(branches) - 1; i >= 0; i--)
		if (branches[i].pinned)
			root_result[i] = recover_root(&branches[i]);
	for (i = ARRAY_SIZE(branches) - 1; i >= 0; i--) {
		struct branch *branch = &branches[i];

		if (!branch->pinned)
			continue;
		ret = root_result[i] ?: recover_branch(branch);
		branch->verified = !ret;
		pr_info("%s %s recovery=%d\n", branch->name, phase, ret);
	}
}

static int pair_sleep(void)
{
	int i, ret;

	ret = conditions();
	for (i = 0; !ret && i < ARRAY_SIZE(branches); i++)
		ret = begin_request(&branches[i]);
	for (i = 0; !ret && i < ARRAY_SIZE(branches); i++)
		ret = verify_ack(&branches[i]);
	for (i = 0; !ret && i < ARRAY_SIZE(branches); i++)
		ret = clear_request(&branches[i]);
	for (i = 0; !ret && i < ARRAY_SIZE(branches); i++)
		ret = verify_link_start(&branches[i]);
	for (i = 0; !ret && i < ARRAY_SIZE(branches); i++)
		ret = disable_link(&branches[i]);
	for (i = 0; !ret && i < ARRAY_SIZE(branches); i++)
		ret = wait_root_link(&branches[i], true, PCI_D0, 500000, "Link Disable confirmed");
	for (i = 0; !ret && i < ARRAY_SIZE(branches); i++)
		ret = wait_root_link(&branches[i], true, PCI_D0, 0, "Link Disable before GPIO");
	for (i = 0; !ret && i < ARRAY_SIZE(branches); i++)
		ret = power_off(&branches[i]);
	for (i = 0; !ret && i < ARRAY_SIZE(branches); i++)
		ret = verify_off(&branches[i], "before s2idle", false);
	if (!ret) {
		pair_powered_off = true;
		pr_info("both branches passed the physical-control and link-off checks\n");
	} else {
		pr_warn("paired sleep aborted (%d); recovering both branches\n", ret);
		recover_pair("suspend rollback");
	}
	return ret;
}

static int trial_prepare(struct device *dev)
{
	int ret = native_pm->prepare(dev);

	return READ_ONCE(trial) && ret > 0 ? 0 : ret;
}

static int trial_suspend_noirq(struct device *dev)
{
	int ret = native_pm->suspend_noirq(dev);

	if (ret || !READ_ONCE(trial) ||
	    pm_suspend_target_state != PM_SUSPEND_TO_IDLE || !pm_suspend_no_platform())
		return ret;
	mutex_lock(&transition_lock);
	/* Unexpected state: keep the controllers powered and sleep normally. */
	if (dev->driver || !to_pci_dev(dev)->state_saved)
		skip_power = true;
	/* pair_sleep() recovers both branches itself if a step fails. */
	if (++suspended_roots == ARRAY_SIZE(branches) && !skip_power)
		pair_sleep();
	mutex_unlock(&transition_lock);
	return ret;
}

static int trial_resume_noirq(struct device *dev)
{
	int i, ret;

	if (!READ_ONCE(trial))
		return native_pm->resume_noirq(dev);
	mutex_lock(&transition_lock);
	if (!resumed_roots) {
		ktime_t elapsed = ktime_mono_to_any(0, TK_OFFS_BOOT) - before_sleep;

		WRITE_ONCE(sleep_ns, max_t(ktime_t, elapsed, 0));
		WRITE_ONCE(wake_mono_ns, ktime_get_ns());
		pr_info("timekeeping reports %llu ns in suspend\n", sleep_ns);
	}
	if (!resumed_roots && pair_powered_off) {
		if (READ_ONCE(sleep_ns) < 100 * NSEC_PER_MSEC) {
			pr_warn("paired power-down completed but no actual sleep was measured\n");
			pair_powered_off = false;
		}
		for (i = 0; i < ARRAY_SIZE(branches); i++)
			if (verify_off(&branches[i], "before early wake", false)) {
				pr_warn("%s was already out of the verified off state at wake\n",
					branches[i].name);
				pair_powered_off = false;
			}
	}
	ret = native_pm->resume_noirq(dev);
	if (++resumed_roots == ARRAY_SIZE(branches))
		recover_pair("early wake");
	mutex_unlock(&transition_lock);
	return ret;
}

static int external_usb(struct usb_device *udev, void *unused)
{
	struct device *controller = udev->bus->controller;

	if (!udev->parent)
		return 0;
	/* The only permitted non-root USB device is this MacBook's internal T1. */
	if (controller && !strcmp(dev_name(controller), "0000:00:14.0") &&
	    !udev->parent->parent && udev->portnum == 3 &&
	    le16_to_cpu(udev->descriptor.idVendor) == 0x05ac &&
	    le16_to_cpu(udev->descriptor.idProduct) == 0x8600)
		return 0;
	return -EBUSY;
}

static int unexpected_child(struct pci_dev *pdev, void *data)
{
	struct branch *branch = data;
	enum branch_node i;

	for (i = UPSB; i < NODE_COUNT; i++)
		if (pdev == branch->devices[i])
			return 0;
	branch->extra = true;
	return 1;
}

static int preflight(struct branch *branch)
{
	struct pci_dev *root = branch->devices[ROOT];
	enum branch_node i;
	unsigned long long gnhi, rtbt;

	if (root->dev.driver || root->dev.pm_domain != &domain ||
	    root->current_state != PCI_D0 || !root->subordinate ||
	    root->subordinate != branch->devices[UPSB]->bus)
		return -EBUSY;
	for (i = ROOT; i < NODE_COUNT; i++)
		if (!device_is_registered(&branch->devices[i]->dev))
			return -ENODEV;
	for (i = ROOT; i < NHI; i++)
		if (!branch->devices[i]->subordinate ||
		    branch->devices[i]->subordinate->ops != root->bus->ops)
			return -ENODEV;
	for (i = UPSB; i < NODE_COUNT; i++) {
		struct device *dev = &branch->devices[i]->dev;

		if (dev->pm_domain || device_has_driver_override(dev) ||
		    (i < NHI && dev->driver))
			return -EBUSY;
	}
	if (!branch->devices[NHI]->dev.driver || !branch->devices[XHC]->dev.driver ||
	    strcmp(branch->devices[NHI]->dev.driver->name, "thunderbolt") ||
	    strcmp(branch->devices[XHC]->dev.driver->name, "xhci_hcd"))
		return -EBUSY;
	branch->extra = false;
	pci_walk_bus(root->subordinate, unexpected_child, branch);
	if (branch->extra)
		return -EBUSY;
	for (i = ROOT; i < NODE_COUNT; i++)
		if (save_addresses(branch, i) ||
		    (i < NHI && bridge_route(branch, i, true, false)))
			return -ENODEV;
	if (identity(branch, NHI) || identity(branch, XHC) ||
	    aml(branch->root_handle, "GNHI", 0, 0, 0, &gnhi) ||
	    aml(branch->root_handle, "RTBT", 0, 0, 0, &rtbt) ||
	    !(gnhi || rtbt))
		return -EAGAIN;
	if (pad_input(branch->nhi_pad) || pad_input(branch->usb_pad) ||
	    pad_input(branch->reset_pad))
		return -EAGAIN;
	return 0;
}

static void set_fences(struct branch *branch, bool enable)
{
	int i;

	for (i = ROOT; i < NHI; i++) {
		struct guarded_bus *entry = &branch->buses[i];

		if (enable) {
			entry->bus = branch->devices[i]->subordinate;
			entry->original = pci_bus_set_ops(entry->bus, &fenced_ops);
		} else {
			pci_bus_set_ops(entry->bus, entry->original);
		}
	}
	branch->fenced = enable;
}

static int isolate_branch(struct branch *branch)
{
	enum branch_node i;
	int ret;

	/* Module and runtime references are taken once and kept until reboot. */
	if (!branch->held_once)
		__module_get(THIS_MODULE);
	branch->pinned = true;

	for (i = UPSB; i < NODE_COUNT; i++) {
		struct device *dev = &branch->devices[i]->dev;
		struct retained_device *saved = &branch->retained[i];

		if (saved->usage) {
			ret = pm_runtime_resume(dev);
		} else {
			ret = pm_runtime_resume_and_get(dev);
			if (ret >= 0)
				saved->usage = true;
		}
		if (ret < 0)
			return ret;
		ret = device_set_driver_override(dev, "mbp142-tb-retained-quarantine");
		if (ret)
			return ret;
		saved->override = true;
	}
	for (i = XHC; i >= NHI; i--)
		device_release_driver(&branch->devices[i]->dev);
	for (i = UPSB; i < NODE_COUNT; i++) {
		struct pci_dev *pdev = branch->devices[i];
		struct device *dev = &pdev->dev;
		struct retained_device *saved = &branch->retained[i];
		u16 command;

		__pm_runtime_disable(dev, false);
		saved->disabled = true;
		device_lock(dev);
		if (dev->driver || dev->pm_domain) {
			device_unlock(dev);
			return -EBUSY;
		}
		dev_pm_domain_set(dev, &isolated_domain);
		device_unlock(dev);
		saved->isolated = true;

		if (i >= NHI &&
		    (pci_read_config_word(pdev, PCI_COMMAND, &command) ||
		     command & PCI_COMMAND_MASTER || pci_dev_msi_enabled(pdev)))
			return -EIO;
		ret = pci_save_state(pdev);
		if (ret)
			return ret;
		saved->saved = true;
	}
	if (verify_addresses(branch))
		return -EIO;
	set_fences(branch, true);
	pr_info("%s original PCI devices and resources retained; drivers and PCI PM isolated\n",
		branch->name);
	return 0;
}

static int restore_config_and_drivers(struct branch *branch)
{
	enum branch_node i;
	bool runtime_held = false;
	int ret;

	/* A failed branch remains fenced, with runtime PM disabled and no driver. */
	if (!branch->verified)
		return -EIO;
	if (branch->fenced)
		set_fences(branch, false);

	for (i = UPSB; i < NODE_COUNT; i++)
		if (branch->retained[i].saved)
			pci_restore_state(branch->devices[i]);
	ret = verify_addresses(branch);
	if (ret) {
		set_fences(branch, true);
		return ret;
	}
	for (i = UPSB; i < NODE_COUNT; i++) {
		struct device *dev = &branch->devices[i]->dev;
		struct retained_device *saved = &branch->retained[i];

		if (saved->isolated) {
			device_lock(dev);
			dev_pm_domain_set(dev, NULL);
			device_unlock(dev);
			saved->isolated = false;
		}
		if (saved->disabled) {
			pm_runtime_enable(dev);
			saved->disabled = false;
		}
		if (saved->override) {
			if (device_set_driver_override(dev, "")) {
				pr_warn("%s %s could not clear driver override\n",
					branch->name, dev_name(dev));
				ret = -EIO;
				continue;
			}
			saved->override = false;
		}
		if (i >= NHI && !dev->driver) {
			int attached = device_attach(dev);

			if (attached <= 0) {
				pr_warn("%s %s driver attach returned %d\n",
					branch->name, dev_name(dev), attached);
				ret = -EIO;
			}
		}
		if (i >= NHI) {
			const char *expected = i == NHI ? "thunderbolt" : "xhci_hcd";

			device_lock(dev);
			if (!dev->driver || strcmp(dev->driver->name, expected))
				ret = -EIO;
			device_unlock(dev);
		}
	}
	if (verify_addresses(branch))
		ret = -EIO;
	for (i = UPSB; i < NODE_COUNT; i++)
		if (branch->retained[i].usage) {
			struct device *dev = &branch->devices[i]->dev;

			/* Keep the successful or rolled-back test topology awake
			 * until reboot. A late autosuspend/resume killed RP09 in
			 * the previous test despite an identical initial snapshot.
			 */
			runtime_held = true;
			if (i >= NHI && (!pm_runtime_active(dev) ||
					 atomic_read(&dev->power.usage_count) <= 0))
				ret = -EIO;
		}

	branch->pinned = false;
	if (runtime_held) {
		if (!branch->held_once)
			pr_info("%s PCI runtime references retained for this boot\n", branch->name);
		branch->held_once = true;
	} else if (!branch->held_once) {
		module_put(THIS_MODULE);
	}
	return ret;
}

static void restore_devices(void)
{
	int i, ret;

	pci_lock_rescan_remove();
	recover_pair("final recovery");
	for (i = 0; i < ARRAY_SIZE(branches); i++) {
		struct branch *branch = &branches[i];
		if (!branch->pinned)
			continue;
		ret = restore_config_and_drivers(branch);
		if (ret) {
			pr_err("%s recovery failed (%d), isolation retained=%d\n",
			       branch->name, ret, branch->pinned);
			WRITE_ONCE(degraded, true);
			continue;
		}
		pr_info("%s original PCI addresses verified; endpoint drivers restored\n",
			branch->name);
	}
	pci_unlock_rescan_remove();
}

static bool hotplug_is_disabled(void)
{
	struct file *file;
	loff_t offset = 0;
	char value[2];
	ssize_t count;

	file = filp_open("/sys/module/acpiphp/parameters/disable", O_RDONLY, 0);
	if (IS_ERR(file))
		return false;
	count = kernel_read(file, value, sizeof(value), &offset);
	filp_close(file, NULL);
	return count == sizeof(value) && value[0] == 'Y' && value[1] == '\n';
}

/*
 * Awake power-down of a single branch (prototype, manual trigger).
 * Apple signals a plug event on a powered-down controller through a GPIO
 * whose GPE (_L32 for RP05, _L33 for RP09) notifies NHI0 with Bus Check.
 * That notification powers the branch back on.
 */
static DEFINE_MUTEX(awake_lock);
static unsigned int awake_events;
module_param(awake_events, uint, 0444);

/* Any non-root USB device on this branch's Alpine Ridge xHCI. */
static int branch_usb_device(struct usb_device *udev, void *data)
{
	struct branch *branch = data;

	return udev->parent && udev->bus->controller == &branch->devices[XHC]->dev ?
	       -EBUSY : 0;
}

static int awake_off_one(struct branch *branch)
{
	int ret;

	if (branch->awake_off)
		return 0;
	ret = conditions();
	if (!ret && usb_for_each_dev(branch, branch_usb_device))
		ret = -EBUSY;
	pci_lock_rescan_remove();
	branch->verified = false;
	if (!ret)
		ret = preflight(branch);
	if (ret) {
		pci_unlock_rescan_remove();
		pr_info("%s awake power-down not started (%d)\n", branch->name, ret);
		return ret;
	}
	ret = isolate_branch(branch);
	if (!ret)
		ret = begin_request(branch);
	if (!ret)
		ret = verify_ack(branch);
	if (!ret)
		ret = clear_request(branch);
	if (!ret)
		ret = verify_link_start(branch);
	if (!ret)
		ret = disable_link(branch);
	if (!ret)
		ret = wait_root_link(branch, true, PCI_D0, 500000, "awake Link Disable confirmed");
	if (!ret)
		ret = power_off(branch);
	if (!ret)
		ret = verify_off(branch, "awake off", false);
	if (!ret) {
		branch->awake_off = true;
		pci_unlock_rescan_remove();
		pr_info("%s powered off while awake\n", branch->name);
		return 0;
	}
	pr_warn("%s awake power-down failed (%d); restoring\n", branch->name, ret);
	ret = recover_root(branch) ?: recover_branch(branch);
	branch->verified = !ret;
	if (ret || restore_config_and_drivers(branch)) {
		WRITE_ONCE(degraded, true);
		WRITE_ONCE(enabled, false);
		pr_err("%s restore after failed awake power-down incomplete\n", branch->name);
	}
	pci_unlock_rescan_remove();
	return -EIO;
}

static int awake_on_one(struct branch *branch, const char *why)
{
	int ret;

	if (!branch->awake_off)
		return 0;
	pci_lock_rescan_remove();
	ret = recover_root(branch) ?: recover_branch(branch);
	branch->verified = !ret;
	pr_info("%s awake power-up (%s) recovery=%d\n", branch->name, why, ret);
	if (!ret)
		ret = restore_config_and_drivers(branch);
	pci_unlock_rescan_remove();
	branch->awake_off = false;
	if (ret) {
		WRITE_ONCE(degraded, true);
		WRITE_ONCE(enabled, false);
		pr_err("%s awake power-up incomplete (%d); attempts disabled\n",
		       branch->name, ret);
	} else {
		pr_info("%s back on, endpoint drivers restored\n", branch->name);
	}
	return ret;
}

static void awake_wake_work(struct work_struct *work)
{
	unsigned int flags = lock_system_sleep();
	int i;

	mutex_lock(&awake_lock);
	for (i = 0; i < ARRAY_SIZE(branches); i++)
		if (branches[i].wake_pending) {
			branches[i].wake_pending = false;
			awake_on_one(&branches[i], "hotplug event");
		}
	mutex_unlock(&awake_lock);
	unlock_system_sleep(flags);
}
static DECLARE_WORK(awake_work, awake_wake_work);

static void awake_notify(acpi_handle handle, u32 event, void *data)
{
	struct branch *branch = data;

	awake_events++;
	pr_info("%s ACPI notify %#x (awake_off=%d)\n", branch->name, event, branch->awake_off);
	if (READ_ONCE(branch->awake_off)) {
		branch->wake_pending = true;
		schedule_work(&awake_work);
	}
}

static void awake_all_on(const char *why)
{
	int i;

	mutex_lock(&awake_lock);
	for (i = 0; i < ARRAY_SIZE(branches); i++)
		awake_on_one(&branches[i], why);
	mutex_unlock(&awake_lock);
}

/* Write "RP05", "RP09" (power that branch off) or "on" (both back on). */
static int awake_set(const char *value, const struct kernel_param *kp)
{
	unsigned int flags;
	int i, ret = -EINVAL;

	if (!READ_ONCE(enabled) || READ_ONCE(degraded))
		return -EPERM;
	flags = lock_system_sleep();
	mutex_lock(&awake_lock);
	if (sysfs_streq(value, "on")) {
		for (i = 0; i < ARRAY_SIZE(branches); i++)
			awake_on_one(&branches[i], "manual");
		ret = 0;
	}
	for (i = 0; i < ARRAY_SIZE(branches); i++)
		if (sysfs_streq(value, branches[i].name))
			ret = awake_off_one(&branches[i]);
	mutex_unlock(&awake_lock);
	unlock_system_sleep(flags);
	return ret;
}

static int awake_get(char *buf, const struct kernel_param *kp)
{
	return sysfs_emit(buf, "RP05=%s RP09=%s\n",
			  branches[0].awake_off ? "off" : "on",
			  branches[1].awake_off ? "off" : "on");
}

static const struct kernel_param_ops awake_ops = { .set = awake_set, .get = awake_get };
module_param_cb(awake, &awake_ops, NULL, 0644);

static int trial_notify_sleep(struct notifier_block *notifier, unsigned long action, void *data);

static int trial_notify(struct notifier_block *notifier, unsigned long action, void *data)
{
	/* The proven sleep path starts from powered branches. */
	if (action == PM_SUSPEND_PREPARE || action == PM_HIBERNATION_PREPARE)
		awake_all_on("before sleep");
	return trial_notify_sleep(notifier, action, data);
}

static int trial_notify_sleep(struct notifier_block *notifier, unsigned long action, void *data)
{
	int i, ret;

	if (action == PM_POST_SUSPEND && READ_ONCE(trial)) {
		bool early_ok = branches[0].verified && branches[1].verified;

		restore_devices();
		WRITE_ONCE(trial, false);
		WRITE_ONCE(succeeded, pair_powered_off && early_ok && !READ_ONCE(degraded) &&
			   branches[0].verified && branches[1].verified);
		if (READ_ONCE(succeeded))
			powered_off++;
		if (READ_ONCE(degraded)) {
			WRITE_ONCE(enabled, false);
			pr_err("recovery incomplete; further power-down attempts disabled\n");
		}
		pr_info("cycle %u: powered_off=%d succeeded=%d sleep_ns=%llu\n", cycles,
			pair_powered_off, READ_ONCE(succeeded), READ_ONCE(sleep_ns));
		WRITE_ONCE(finished, true);
		return NOTIFY_OK;
	}
	/* Never veto a sleep request; on any doubt the controllers stay powered. */
	if (action != PM_SUSPEND_PREPARE || !READ_ONCE(enabled) || READ_ONCE(degraded))
		return NOTIFY_DONE;
	cycles++;
	WRITE_ONCE(finished, false);
	WRITE_ONCE(succeeded, false);
	if (!pm_suspend_default_s2idle() || usb_for_each_dev(NULL, external_usb) ||
	    conditions()) {
		pr_info("s2idle, external USB or firmware check not met; controllers stay powered\n");
		skipped++;
		WRITE_ONCE(finished, true);
		return NOTIFY_DONE;
	}
	pci_lock_rescan_remove();
	for (i = 0; i < ARRAY_SIZE(branches); i++) {
		branches[i].verified = false;
		ret = preflight(&branches[i]);
		if (ret) {
			pr_info("%s preflight not met (%d); controllers stay powered\n",
				branches[i].name, ret);
			goto abort;
		}
	}
	for (i = 0; i < ARRAY_SIZE(branches); i++) {
		ret = isolate_branch(&branches[i]);
		if (ret) {
			pr_warn("%s driver isolation failed (%d); restoring\n",
				branches[i].name, ret);
			goto abort;
		}
	}
	suspended_roots = 0;
	resumed_roots = 0;
	skip_power = false;
	pair_powered_off = false;
	WRITE_ONCE(sleep_ns, 0);
	before_sleep = ktime_mono_to_any(0, TK_OFFS_BOOT);
	WRITE_ONCE(trial, true);
	pci_unlock_rescan_remove();
	return NOTIFY_OK;

abort:
	pci_unlock_rescan_remove();
	restore_devices();
	if (READ_ONCE(degraded)) {
		WRITE_ONCE(enabled, false);
		pr_err("restore after aborted preparation incomplete; attempts disabled\n");
	}
	skipped++;
	WRITE_ONCE(finished, true);
	return NOTIFY_DONE;
}

static struct notifier_block notifier = { .notifier_call = trial_notify };

static int discover(struct branch *branch)
{
	static const struct { enum branch_node parent; u8 slot; u16 id; } children[] = {
		[UPSB] = { ROOT, 0, 0x1578 }, [DSB0] = { UPSB, 0, 0x15d3 },
		[DSB1] = { UPSB, 1, 0x15d3 }, [DSB2] = { UPSB, 2, 0x15d3 },
		[DSB4] = { UPSB, 4, 0x15d3 }, [NHI] = { DSB0, 0, 0x15d2 },
		[XHC] = { DSB2, 0, 0x15d4 },
	};
	struct pci_dev *pdev, *upsb;
	acpi_handle found;
	char path[48];
	enum branch_node i;
	u32 vsec;

	pdev = pci_get_domain_bus_and_slot(0, 0, branch->root_devfn);
	branch->devices[ROOT] = pdev;
	if (!pdev || pdev->vendor != PCI_VENDOR_ID_INTEL || pdev->device != branch->root_id)
		return -ENODEV;
	for (i = UPSB; i < NODE_COUNT; i++) {
		struct pci_bus *bus = branch->devices[children[i].parent]->subordinate;

		if (!bus)
			return -ENODEV;
		pdev = pci_get_slot(bus, PCI_DEVFN(children[i].slot, 0));
		branch->devices[i] = pdev;
		if (!pdev || pdev->vendor != PCI_VENDOR_ID_INTEL ||
		    pdev->device != children[i].id || pdev->revision != 2)
			return -ENODEV;
	}
	upsb = branch->devices[UPSB];
	branch->vsec = pci_find_vsec_capability(upsb, PCI_VENDOR_ID_INTEL, 0x1234);
	if (branch->vsec != 0x500 ||
	    pci_read_config_dword(upsb, branch->vsec + PCI_VNDR_HEADER, &vsec) ||
	    vsec == U32_MAX || PCI_VNDR_HEADER_LEN(vsec) < 0x50)
		return -ENODEV;
	branch->root_handle = ACPI_HANDLE(&branch->devices[ROOT]->dev);
	branch->nhi_handle = ACPI_HANDLE(&branch->devices[NHI]->dev);
	branch->dsb_handle = ACPI_HANDLE(&branch->devices[DSB2]->dev);
	branch->xhc_handle = ACPI_HANDLE(&branch->devices[XHC]->dev);
	if (!branch->root_handle || !branch->nhi_handle ||
	    !branch->dsb_handle || !branch->xhc_handle ||
	    !acpi_has_method(branch->root_handle, "_PS0") ||
	    !acpi_has_method(branch->nhi_handle, "SXFP") ||
	    !acpi_has_method(branch->nhi_handle, "XRST") ||
	    !acpi_has_method(branch->nhi_handle, "TRPE") ||
	    !acpi_has_method(branch->dsb_handle, "_PS0") ||
	    !acpi_has_method(branch->xhc_handle, "_PS0"))
		return -ENODEV;
	snprintf(path, sizeof(path), "\\_SB_.PCI0.%s.UPSB.DSB0.NHI0", branch->name);
	if (ACPI_FAILURE(acpi_get_handle(NULL, path, &found)) || found != branch->nhi_handle)
		return -ENODEV;
	snprintf(path, sizeof(path), "\\_SB_.PCI0.%s.UPSB.DSB2.%s",
		 branch->name, branch->usb_name);
	if (ACPI_FAILURE(acpi_get_handle(NULL, path, &found)) || found != branch->xhc_handle)
		return -ENODEV;
	return 0;
}

static void release_branches(void)
{
	int i, node;

	for (i = 0; i < ARRAY_SIZE(branches); i++) {
		struct branch *branch = &branches[i];

		if (branch->attached) {
			struct device *dev = &branch->devices[ROOT]->dev;

			__pm_runtime_disable(dev, false);
			device_lock(dev);
			dev_pm_domain_set(dev, NULL);
			device_unlock(dev);
			pm_runtime_enable(dev);
		}
		if (branch->notify_installed) {
			acpi_remove_notify_handler(branch->nhi_handle, ACPI_ALL_NOTIFY,
						   awake_notify);
			acpi_remove_notify_handler(branch->root_handle, ACPI_ALL_NOTIFY,
						   awake_notify);
			branch->notify_installed = false;
		}
		for (node = NODE_COUNT - 1; node >= 0; node--)
			pci_dev_put(branch->devices[node]);
	}
}

static int __init pair_init(void)
{
	unsigned int flags;
	int i, ret;

	if (!dmi_match(DMI_SYS_VENDOR, "Apple Inc.") ||
	    !dmi_match(DMI_PRODUCT_NAME, "MacBookPro14,2") ||
	    !dmi_match(DMI_BOARD_NAME, "Mac-CAD6701F7CEA0921"))
		return -ENODEV;
	native_pm = pci_bus_type.pm;
	if (!native_pm || !native_pm->prepare || !native_pm->suspend_noirq ||
	    !native_pm->resume_noirq)
		return -ENODEV;
	domain.ops = *native_pm;
	domain.ops.prepare = trial_prepare;
	domain.ops.suspend_noirq = trial_suspend_noirq;
	domain.ops.resume_noirq = trial_resume_noirq;
	flags = lock_system_sleep();
	pci_lock_rescan_remove();
	for (i = 0; i < ARRAY_SIZE(branches); i++) {
		struct device *dev;

		ret = discover(&branches[i]);
		if (ret)
			goto fail;
		dev = &branches[i].devices[ROOT]->dev;
		__pm_runtime_disable(dev, false);
		device_lock(dev);
		ret = dev->driver || dev->pm_domain ? -EBUSY : 0;
		if (!ret) {
			dev_pm_domain_set(dev, &domain);
			branches[i].attached = true;
		}
		device_unlock(dev);
		pm_runtime_enable(dev);
		if (ret)
			goto fail;
		ret = preflight(&branches[i]);
		if (ret) {
			pr_warn("%s initial preflight failed (%d)\n", branches[i].name, ret);
			goto fail;
		}
	}
	ret = conditions();
	if (ret)
		goto fail;
	ret = register_pm_notifier(&notifier);
	if (ret)
		goto fail;
	for (i = 0; i < ARRAY_SIZE(branches); i++) {
		struct branch *branch = &branches[i];

		if (ACPI_FAILURE(acpi_install_notify_handler(branch->nhi_handle, ACPI_ALL_NOTIFY,
							     awake_notify, branch)))
			pr_warn("%s NHI0 notify handler not installed\n", branch->name);
		else if (ACPI_FAILURE(acpi_install_notify_handler(branch->root_handle,
								  ACPI_ALL_NOTIFY,
								  awake_notify, branch)))
			acpi_remove_notify_handler(branch->nhi_handle, ACPI_ALL_NOTIFY,
						   awake_notify);
		else
			branch->notify_installed = true;
	}
	pr_info("both branch identities and firmware methods verified; enabled=%d acpiphp_disabled=%d\n",
		enabled, hotplug_is_disabled());
	goto unlock;
fail:
	release_branches();
unlock:
	pci_unlock_rescan_remove();
	unlock_system_sleep(flags);
	return ret;
}

static void __exit pair_exit(void)
{
	unsigned int flags = lock_system_sleep();

	awake_all_on("module unload");
	cancel_work_sync(&awake_work);
	unregister_pm_notifier(&notifier);
	pci_lock_rescan_remove();
	release_branches();
	pci_unlock_rescan_remove();
	unlock_system_sleep(flags);
}

module_init(pair_init);
module_exit(pair_exit);
MODULE_LICENSE("GPL");
MODULE_VERSION("5-awake");
MODULE_DESCRIPTION("MacBookPro14,2: power both idle Thunderbolt controllers off during s2idle");
