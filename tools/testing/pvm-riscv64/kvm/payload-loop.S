/* SPDX-License-Identifier: GPL-2.0 */
/* Negative-test N4 payload, two phases:
 *   1. readiness via SBI DBCN write of one byte 'R' (fid 2) — the
 *      backend forwards this to userspace, the child VMM answers it
 *      and writes the pipe; on the next KVM_RUN the backend performs
 *      the sbi_pending PC+4 resume and execution proceeds;
 *   2. spin in U-mode forever until the parent SIGTERMs the child. */
	.globl _start
_start:
	li	a0, 82			/* 'R' — DBCN write_byte takes the byte
				 * itself in a0, not a buffer+len */
	li	a7, 0x4442434e		/* SBI DBCN */
	li	a6, 2			/* fid: write_byte */
	ecall
1:	j	1b			/* phase 2: infinite U loop */
