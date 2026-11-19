// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

.section .text
.global hk_flush_icache
.type hk_flush_icache, %function
hk_flush_icache:
	ic ivau, x0
	dsb ish
	isb
	ret
.size hk_flush_icache, . - hk_flush_icache
