/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SUSFS_CREATE_GATE_H
#define __SUSFS_CREATE_GATE_H

/* The create family's entry layer (susfs_create_gate.c): answers EACCES for a hidden name
 * where filename_create() would have answered EEXIST before any LSM hook could run. */
int susfs_create_gate_init(void);
void susfs_create_gate_exit(void);

#endif /* __SUSFS_CREATE_GATE_H */
