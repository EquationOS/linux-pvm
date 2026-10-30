/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Stage-C smoke payload (two mandatory regressions in one run):
 *
 *  1. Cross-page instruction: a 4-byte uncompressed instruction sits
 *     at 0x80200ffe so its fetch faults on the NEXT page (tval
 *     0x80201000).  The backend must fill the page named by stval,
 *     not the PC page, and the payload must continue correctly.
 *  2. WFI wake under global SIE=0: enable only the local timer
 *     interrupt (sie.STIE), clear sstatus.SIE, arm SBI TIME, wfi.
 *     The hart must wake (no deadlock) and NO interrupt may be
 *     injected while the virtual global SIE is clear -- the payload
 *     then reports via SBI SRST with the stage-C magic.
 */
#define PVM_STAGE_C_MAGIC 0x50564D4B564D4D43 /* "PVMKVMC" */

	.globl _start
_start:
	j	skip

	/* gap never executed */
	.org	0xffe
skip:
	.option	push
	.option	norvc
	nop				/* 4B insn at 0x80200ffe, crosses to 0x80201000 */
	.option	pop

	/* now at 0x80201002: regression 2 */
	li	t0, 0x20			/* STIE */
	csrw	sie, t0
	csrc	sstatus, 0x2			/* global SIE off */
	rdtime	t1
	li	t2, 300000			/* 30ms @10MHz */
	add	t1, t1, t2
	li	a7, 0x54494d45			/* SBI TIME */
	li	a6, 0
	mv	a0, t1
	ecall
	wfi					/* must wake, must not take an IRQ */

	li	a7, 0x53525354			/* SBI SRST */
	li	a6, 0
	li	a0, 0				/* shutdown */
	li	a1, PVM_STAGE_C_MAGIC
	ecall
1:	j	1b
