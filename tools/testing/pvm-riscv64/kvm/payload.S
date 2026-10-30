/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Stage-A smoke payload: entered by the PVM KVM backend at GPA
 * 0x80200000 in physical U-mode with bare satp.  It does exactly one
 * thing: an SBI SRST ecall, which the S-mode host backend turns into
 * KVM_EXIT_SYSTEM_EVENT (type=SHUTDOWN, data[0]=PVM_STAGE_A_MAGIC).
 * If the ecall were executed by anything else the magic never
 * arrives and the VMM reports failure.
 */
#define PVM_STAGE_A_MAGIC 0x50564D554D4F4445 /* "PVMUMODE" */

	.globl _start
_start:
	li	a7, 0x53525354		/* SBI_EXT_SRST */
	li	a6, 0			/* fid: system_reset */
	li	a0, 0			/* SHUTDOWN */
	li	a1, PVM_STAGE_A_MAGIC	/* marker carried via system_event.data[0] */
	ecall
1:	j	1b			/* must never run past the ecall */
