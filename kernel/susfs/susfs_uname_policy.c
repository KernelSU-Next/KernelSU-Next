/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/string.h>
#include "susfs_log.h"
#include "susfs_hooks.h"
#include "susfs_uname_data.h"
#include "susfs_uname_policy.h"
#include "susfs_uname_phys.h"

bool susfs_uname_snapshot(long result, bool has_buffer, struct susfs_uname_values *out)
{
	if (!susfs_uname_should_apply(result, susfs_uname_enabled, has_buffer))
		return false;
	*out = susfs_uname_state_values;
	return true;
}

static int uname_register(struct susfs_hook *hook)
{
	int rc;

	/* Preserve the existing arm/disarm serialization. String parameter writes
	 * are not covered by this mutex and this refactor does not make them atomic. */
	mutex_lock(&susfs_uname_ctl_lock);
	if (susfs_uname_registered) {
		mutex_unlock(&susfs_uname_ctl_lock);
		return 0;
	}
	rc = susfs_hook_register(hook);
	if (rc < 0) {
		mutex_unlock(&susfs_uname_ctl_lock);
		return rc;
	}
	susfs_uname_registered = true;
	susfs_uname_inline = hook->backend == SUSFS_HOOK_BACKEND_INLINE;
	mutex_unlock(&susfs_uname_ctl_lock);
	if (susfs_uname_inline)
		SUSFS_LOGI("uname spoof armed inline: release=%s version=%s\n",
			   susfs_uname_state_values.release, susfs_uname_state_values.version);
	else
		SUSFS_LOGI("uname spoof armed: release=%s version=%s\n",
			   susfs_uname_state_values.release, susfs_uname_state_values.version);
	return 0;
}

void susfs_uname_policy_init(struct susfs_hook *hook)
{
	susfs_uname_data_pad();
	if (susfs_uname_enabled && susfs_uname_state_values.release[0] &&
	    susfs_uname_state_values.version[0]) {
		int rc = uname_register(hook);

		if (rc)
			pr_warn("uname: hook registration failed %d\n", rc);
	} else {
		SUSFS_LOGI("uname spoof: disabled (enable via CMD_SUSFS_SET_UNAME)\n");
	}
}

void susfs_uname_policy_exit(struct susfs_hook *hook)
{
	mutex_lock(&susfs_uname_ctl_lock);
	if (susfs_uname_registered) {
		susfs_hook_unregister(hook);
		susfs_uname_registered = false;
		susfs_uname_inline = false;
	}
	mutex_unlock(&susfs_uname_ctl_lock);
}

int susfs_uname_set(struct susfs_hook *hook, const struct susfs_uname_values *input)
{
	struct susfs_uname_values current_values;
	bool default_release = !strcmp(input->release, "default");
	bool default_version = !strcmp(input->version, "default");
	int rc;

	if (default_release || default_version)
		susfs_uname_read_current(&current_values);
	strscpy_pad(susfs_uname_state_values.release,
		    default_release ? current_values.release : input->release,
		    SUSFS_UNAME_LEN);
	strscpy_pad(susfs_uname_state_values.version,
		    default_version ? current_values.version : input->version,
		    SUSFS_UNAME_LEN);
	/* Existing semantics: an arm failure leaves the requested values/enabled
	 * flag set, and the next command retries registration. */
	susfs_uname_enabled = true;
	rc = uname_register(hook);
	if (rc)
		return rc;
	SUSFS_LOGI("uname spoof set: release=%s version=%s\n",
		   susfs_uname_state_values.release, susfs_uname_state_values.version);
	return 0;
}
