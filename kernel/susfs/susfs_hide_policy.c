/* SPDX-License-Identifier: GPL-2.0-only */
/* Commands and decisions; no seq_file, kprobe or kernel object layouts. */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/string.h>
#include "susfs_log.h"
#include "susfs_core.h"
#include "susfs_hide_data.h"
#include "susfs_hide_policy.h"
#include "susfs_hide_phys.h"
#include "susfs_hide_view.h"

static int hide_modules_parse(const char *val, char dst[][MODULE_NAME_LEN], int max)
{
	char *buf;
	const char *p;
	int n = 0;
	int rc;

	buf = kvmalloc(HIDE_MODULES_CMDLINE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	strscpy(buf, val, HIDE_MODULES_CMDLINE);
	p = buf;
	while (*p) {
		char *tok = (char *)p;
		int len;

		while (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n' || *p == '\r')
			p++;
		if (!*p)
			break;
		tok = (char *)p;
		while (*p && *p != ' ' && *p != ',' && *p != '\t' && *p != '\n' && *p != '\r')
			p++;
		len = (int)(p - tok);
		if (len <= 0)
			continue;
		if (len >= MODULE_NAME_LEN) {
			rc = -ENAMETOOLONG;	/* longer than any module name */
			goto out;
		}
		if (n >= max) {
			rc = -ENOSPC;
			goto out;
		}
		memcpy(dst[n], tok, len);
		dst[n][len] = '\0';
		n++;
	}
	rc = n;
out:
	kvfree(buf);
	return rc;
}

int hide_modules_command(const char *val, bool bare_list)
{
	char *cmd;
	char (*staged)[MODULE_NAME_LEN];
	const char *arg;
	int i, n;
	int rc = 0;

	cmd = kvmalloc(HIDE_MODULES_CMDLINE, GFP_KERNEL);
	staged = kvmalloc_array(HIDE_MODULES_MAX, MODULE_NAME_LEN, GFP_KERNEL);
	if (!cmd || !staged) {
		kvfree(cmd);
		kvfree(staged);
		return -ENOMEM;
	}

	strscpy(cmd, val, HIDE_MODULES_CMDLINE);
	for (i = (int)strlen(cmd) - 1; i >= 0 && (cmd[i] == '\n' || cmd[i] == '\r' || cmd[i] == ' '); i--)
		cmd[i] = '\0';

	if (!strcmp(cmd, "clear")) {
		hide_modules_commit(staged, 0);
		hide_modules_sync_sysfs();
		SUSFS_LOGI("hide_modules: list cleared (no module is filtered)\n");
		goto out;
	}

	if (!strncmp(cmd, "set ", 4)) {
		n = hide_modules_parse(cmd + 4, staged, HIDE_MODULES_MAX);
		if (n < 0) {
			rc = n;
			goto out;
		}
		hide_modules_commit(staged, n);
		hide_modules_sync_sysfs();
		SUSFS_LOGI("hide_modules: list set to %d name(s)\n", n);
		goto out;
	}

	if (!strncmp(cmd, "add ", 4) || !strncmp(cmd, "del ", 4)) {
		bool adding = (cmd[0] == 'a');

		arg = cmd + 4;
		while (*arg == ' ')
			arg++;
		if (!*arg || strlen(arg) >= MODULE_NAME_LEN) {
			rc = -EINVAL;
			goto out;
		}

		spin_lock(&hide_modules_lock);
		n = n_hide_modules;
		if (n > HIDE_MODULES_MAX)
			n = HIDE_MODULES_MAX;
		memcpy(staged, hide_modules, (size_t)n * MODULE_NAME_LEN);
		spin_unlock(&hide_modules_lock);

		/* `found` is kept separate from `i` on purpose: after a del the index and the new count coincide whenever the
		 * removed entry was the last one, and reusing `i` for both questions answered -ENOENT for a name that was right
		 * there (measured: `del <last entry>` always failed). */
		{
			bool found = false;

			for (i = 0; i < n; i++) {
				if (strcmp(staged[i], arg))
					continue;
				found = true;
				if (adding)
					goto out;	/* already listed */
				memmove(&staged[i], &staged[i + 1],
					(size_t)(n - i - 1) * MODULE_NAME_LEN);
				n--;
				break;
			}
			if (adding) {
				if (found)
					goto out;
				if (n >= HIDE_MODULES_MAX) {
					rc = -ENOSPC;
					goto out;
				}
				strscpy(staged[n], arg, MODULE_NAME_LEN);
				n++;
			} else if (!found) {
				rc = -ENOENT;		/* not listed */
				goto out;
			}
		}
		hide_modules_commit(staged, n);
		hide_modules_sync_sysfs();
		SUSFS_LOGI("hide_modules: %s %s -> %d name(s) filtered\n",
			adding ? "add" : "del", arg, n);
		goto out;
	}

	if (!bare_list) {
		rc = -EINVAL;
		goto out;
	}

	n = hide_modules_parse(cmd, staged, HIDE_MODULES_MAX);
	if (n < 0) {
		rc = n;
		goto out;
	}
	hide_modules_commit(staged, n);
	hide_modules_sync_sysfs();
	SUSFS_LOGI("hide_modules: list set to %d name(s)\n", n);

out:
	kvfree(staged);
	kvfree(cmd);
	return rc;
}

static int hide_modules_format_unlocked(char *buf, size_t size)
{
	int n = 0;
	int i;

	n += scnprintf(buf + n, size - n,
		"hide_modules: %d/%d name(s), /sys/module rules=%d (failed=%d), "
		"/proc/modules lines removed=%d, kallsyms lines removed=%d\n",
		n_hide_modules, HIDE_MODULES_MAX, n_hide_modules_applied,
		atomic_read(&n_sysfs_rules_failed),
		atomic_read(&n_modlines_skipped), atomic_read(&n_kallsyms_mod_skipped));
	n += scnprintf(buf + n, size - n, "names:");
	for (i = 0; i < n_hide_modules && n < (int)size - 64; i++)
		n += scnprintf(buf + n, size - n, " %s", hide_modules[i]);
	n += scnprintf(buf + n, size - n, "\n");
	return n;
}

int hide_modules_format(char *buf, size_t size)
{
	unsigned long flags;
	int n;

	spin_lock_irqsave(&hide_modules_lock, flags);
	n = hide_modules_format_unlocked(buf, size);
	spin_unlock_irqrestore(&hide_modules_lock, flags);
	return n;
}

static int hide_modules_list_check(const char *list)
{
	char (*dst)[MODULE_NAME_LEN];
	int rc;

	dst = kvmalloc_array(HIDE_MODULES_MAX, MODULE_NAME_LEN, GFP_KERNEL);
	if (!dst)
		return -ENOMEM;
	rc = hide_modules_parse(list, dst, HIDE_MODULES_MAX);
	kvfree(dst);
	return rc < 0 ? rc : 0;
}

static int hide_modules_check(char *val)
{
	const char *arg;
	size_t i;

	for (i = strlen(val); i > 0 && (val[i - 1] == '\n' || val[i - 1] == '\r' || val[i - 1] == ' '); i--)
		val[i - 1] = '\0';

	if (!val[0] || !strcmp(val, "clear"))
		return 0;
	if (!strncmp(val, "set ", 4))
		return hide_modules_list_check(val + 4);
	if (!strncmp(val, "add ", 4) || !strncmp(val, "del ", 4)) {
		arg = val + 4;
		while (*arg == ' ')
			arg++;
		if (!*arg || strlen(arg) >= MODULE_NAME_LEN)
			return -EINVAL;
		/* parse_args runs before the table has a single name in it, so `del` here could only fail later
		 * (-ENOENT, "not listed").  Reject it at the insmod instead - that is what it used to do. */
		if (val[0] == 'd')
			return -EINVAL;
		return 0;
	}
	return hide_modules_list_check(val);
}

/* parse_args runs before the imports guard and feature init: validate/cache
 * only, then apply after sus_path is up. Keep the original mutex ordering.
 * The existing pre-guard dependency on mutex_lock/unlock is unchanged. */
int susfs_hide_param_set(const char *val)
{
	int rc;

	mutex_lock(&hide_syms_param_lock);
	if (hide_syms_ready) {
		mutex_unlock(&hide_syms_param_lock);
		return hide_modules_command(val, true);	/* module is up: apply live */
	}

	/* parse_args(): validate the shape and cache the raw value - the command form is part of the
	 * parameter's contract, so it is applied later with the same entry point (see
	 * hide_modules_pending_val).  A typo still fails the insmod here.  Validate the value itself,
	 * not a copy that was already truncated to the cache size. */
	if (strlen(val) >= sizeof(hide_modules_pending_val)) {
		mutex_unlock(&hide_syms_param_lock);
		return -EINVAL;
	}
	strscpy(hide_modules_pending_val, val, sizeof(hide_modules_pending_val));
	rc = hide_modules_check(hide_modules_pending_val);
	if (!rc)
		hide_modules_pending_given = true;
	mutex_unlock(&hide_syms_param_lock);
	return rc;
}

int susfs_hide_config_init(void)
{
	int rc;

	/* Apply what the insmod parameter cached (see hide_modules_pending_val) FIRST, and before the
	 * kprobe registrations below: those can fail without failing the load (this layer is non-fatal in
	 * the layer table), and the operator's list must not be lost because of an unrelated probe.  The
	 * import guard has passed by now, and sus_path is up (this layer is last), which is what the sysfs
	 * rules need.  From here on the setter applies live. */
	mutex_lock(&hide_syms_param_lock);
	hide_syms_ready = true;
	if (hide_modules_pending_given) {
		rc = hide_modules_command(hide_modules_pending_val, true);
		mutex_unlock(&hide_syms_param_lock);
		if (rc) {
			pr_err("susfs_hide_syms: applying hide_modules=\"%s\" from insmod failed %d\n",
					hide_modules_pending_val, rc);
			return rc;
		}
	} else {
		mutex_unlock(&hide_syms_param_lock);
	}

	return 0;
}

void susfs_hide_seed_self(void)
{
	/* hide_modules starts with just this module in its list: a built-in SUSFS has no module entry, so leaving ours visible
	 * would be a trace upstream does not have.  The sysfs rule can only be registered now, because sus_path is up (this
	 * layer is last in the init table).
	 *
	 * A list that is already populated came from the insmod parameter (applied at the top of this function):
	 * module_param_cb()'s setter runs during load_module()'s parse_args(), i.e. before module_init, so the operator's
	 * names are the whole point of the parameter.  Append our own name to whatever is there instead, and only seed the
	 * default when nothing supplied a list. */
	if (n_hide_modules == 0)
		hide_modules_command(SUSFS_LKM_MODULE_NAME, true);
	else
		hide_modules_command("add " SUSFS_LKM_MODULE_NAME, false);
	hide_modules_sync_sysfs();

}

void susfs_hide_policy_exit(void)
{
	/* The /sys/module rules are ours: drop them here, not in sus_path's teardown - this layer cleans up what it registered. */
	hide_modules_commit(hide_modules_applied, 0);
	hide_modules_sync_sysfs();
	SUSFS_LOGI("susfs_hide_syms: exit enter=%d hit=%d layout_bad=%d (module lines=%d listed modules' syms=%d)\n",
		atomic_read(&hide_enter_count), atomic_read(&hide_hit_count),
		atomic_read(&hide_layout_bad),
		atomic_read(&n_modlines_skipped), atomic_read(&n_kallsyms_mod_skipped));
}

bool susfs_hide_has_modules(void)
{
	return n_hide_modules > 0;
}

bool susfs_hide_symbol_decide(const char *name, const char *module_name)
{
	if (susfs_hide_name_decide_core(name)) {
		atomic_inc(&hide_hit_count);
		return true;
	}
	if (module_name[0] && hide_module_name_match(module_name)) {
		atomic_inc(&n_kallsyms_mod_skipped);
		return true;
	}
	return false;
}

bool susfs_hide_module_decide(const char *name)
{
	if (!hide_module_name_match(name))
		return false;
	atomic_inc(&n_modlines_skipped);
	return true;
}
