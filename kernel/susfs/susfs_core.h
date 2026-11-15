/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SUSFS_CORE_H
#define __SUSFS_CORE_H

/* Refuse to load when this image's imported symbol addresses are not kernel addresses, i.e. when
 * the loader that put it in did not absolutize them.  Returns 0 when they are all filled in, -EINVAL
 * otherwise (and prints which ones are missing).  Called first in susfs_init().  How much of the
 * table that really covers depends on the variant (see imports_guard.c), and for param_ops_* a
 * refusal cannot prevent the panic that follows - it only puts the reason in the log first. */
int susfs_imports_guard(void);

/* Second half of the same check, called once the symbol resolver is up: every import flagged
 * compare - the ten data imports, see imports_guard.c - whose name kallsyms has exactly once must
 * hold that address, so an image absolutized from the wrong symbol table (a stale kallsyms, or the
 * wrong occurrence of a duplicate name) is refused as well.  The function imports are not compared:
 * in the LLVM-CFI builds &name is a module-local stub there.
 * The definition carries __nocfi (it calls into the kernel through the resolver's function
 * pointers); the attribute belongs on the definition, so it is not repeated here. */
int susfs_imports_crosscheck(void);

/* Wait until no task can still be inside a window it entered before this call - a kprobe
 * handler, or boot_config_proc_show()'s seq_puts().  synchronize_rcu() is NOT enough for
 * either (neither runs in an RCU read-side section); see lsm_hook.c.  Sleeps. */
void susfs_wait_for_readers(void);

/* The module's own name: the /sys/module directory and the modinfo name are both the module file name, and the self-hide
 * rules and the /proc/modules filter have to agree with it. */
#define SUSFS_LKM_MODULE_NAME "susfs_guard_lkm"
#define SUSFS_LKM_SYSFS_DIR   "/sys/module/" SUSFS_LKM_MODULE_NAME

/* The build's version string.  It lives here rather than in susfs_main.c because imports_guard.c
 * reports it when it refuses a load - and that happens before susfs_main.c prints its banner. */
#define SUSFS_LKM_VERSION "2.3.0-gki"

#endif /* __SUSFS_CORE_H */
