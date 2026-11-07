// SPDX-License-Identifier: GPL-2.0-only
/*
 * raana_run_multi.c
 *
 * Copyright (C) 2026 dere3046
 */

#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/cpumask.h>

#include "raana.h"

static int raana_worker_thread(void *data)
{
	struct raana_worker *wk = data;

	raana_token_worker(wk);
	complete(&wk->done);
	return 0;
}

int raana_run(struct raana_ctx *ctx)
{
	struct raana_worker *wks;
	struct task_struct *tsk;
	int n, i, started;

	n = num_online_cpus();
	if (n > RAANA_MAX_WORKERS)
		n = RAANA_MAX_WORKERS;
	if (n < 1)
		n = 1;

	wks = kcalloc(n, sizeof(*wks), GFP_KERNEL);
	if (!wks)
		return 0;

	for (i = 0; i < n; i++) {
		wks[i].ctx = ctx;
		wks[i].id = i;
		init_completion(&wks[i].done);
	}

	started = 0;
	for (i = 0; i < n; i++) {
		tsk = kthread_run(raana_worker_thread, &wks[i], "raana%d", i);
		if (IS_ERR(tsk))
			break;
		started++;
	}

	if (!started)
		raana_token_worker(&wks[0]);
	for (i = 0; i < started; i++)
		wait_for_completion(&wks[i].done);

	kfree(wks);
	return ctx->nti != 0;
}
