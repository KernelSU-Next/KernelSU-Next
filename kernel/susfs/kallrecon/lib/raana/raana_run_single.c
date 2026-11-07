// SPDX-License-Identifier: GPL-2.0-only
/*
 * raana_run_single.c
 *
 * Copyright (C) 2026 dere3046
 */

#include "raana.h"

int raana_run(struct raana_ctx *ctx)
{
	struct raana_worker wk = {
		.ctx = ctx,
	};

	raana_token_worker(&wk);
	return ctx->nti != 0;
}
