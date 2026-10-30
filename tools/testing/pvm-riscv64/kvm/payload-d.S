/* SPDX-License-Identifier: GPL-2.0 */
/* Executed CSR/privilege assertions. A single trap vector checks the
 * exact interrupted PC, cause and virtual SPP, then returns to a named
 * kernel continuation. All instructions are 4 bytes for the illegal test.
 * The stats window excludes RESET, includes 5 PROBEs and READ: delta 6. */
#define HC_BASE 0x17088200
#define EID 0x70766d
#define PVCS 0x803ff000
#define STATS 0x803fe000
#define MAGIC 0x50564d4b564d4b44
	.option norvc
	.globl _start
_start:
	li tp, 0x803fd000
	li a7, EID
	li a6, HC_BASE+6
	li a0, 1
	li a1, PVCS
	mv a2, tp
	ecall
	li s5, 1
	bnez a0, fail
	la t0, trap_handler
	csrw stvec, t0

	/* Unknown virtual-kernel instruction must be injected, not abort. */
	la s0, illegal_kernel
	li s1, 2
	la s2, after_kernel
	li s3, 0x100
illegal_kernel:
	.word 0xffffffff
after_kernel:
	li s5, 2
	/* WARL: invalid vector mode normalizes to Direct. */
	li t0, 0x80300003
	csrw stvec, t0
	csrr t1, stvec
	li t2, 0x80300000
	bne t1, t2, fail
	la t0, trap_handler
	csrw stvec, t0
	li t0, -1
	csrw scounteren, t0
	csrr t1, scounteren
	li t2, 7
	bne t1, t2, fail
	csrw senvcfg, t0
	csrr t1, senvcfg
	bnez t1, fail
	li t0, 0x1235
	csrw sepc, t0
	csrr t1, sepc
	li t2, 0x1234
	bne t1, t2, fail

	li s5, 3
	/* FS=Off must remain Off; UXL is fixed at 64. */
	csrw sstatus, zero
	csrr t0, sstatus
	li t1, 0x6000
	and t2, t0, t1
	bnez t2, fail
	srli t0, t0, 32
	andi t0, t0, 3
	li t1, 2
	bne t0, t1, fail
	li t0, -1
	csrw sie, t0
	csrr t1, sie
	li t2, 0x22
	bne t1, t2, fail
	csrw sip, t0
	csrr t1, sip
	li t2, 2 /* guest cannot manufacture STIP */
	bne t1, t2, fail
	csrw sip, zero
	csrw sie, zero

	li s5, 4
	/* Bare-mode user fetch is valid; supervisor CSR access is not. */
	la s0, illegal_user
	li s1, 2
	la s2, after_user
	li s3, 0
	csrw sepc, s0
	csrw sstatus, zero
	sret
illegal_user:
	csrr t0, sstatus
	li a7, -1
	ecall /* unexpected cause 8 reaches the kernel handler and fails */
	j illegal_user
after_user:
	li s5, 5
	li a7, EID
	li a6, HC_BASE+12
	ecall
	bnez a0, fail
	li s4, 5
probe_loop:
	li a7, EID
	li a6, HC_BASE+9
	li a0, 1
	ecall
	li t0, 1
	bne a0, t0, fail
	addi s4, s4, -1
	bnez s4, probe_loop
	li a7, EID
	li a6, HC_BASE+13
	li a0, STATS
	ecall
	bnez a0, fail
	li t0, STATS
	ld t1, 16(t0)
	li t2, 6
	bne t1, t2, fail
	ld t1, 112(t0)
	li t2, 2
	bne t1, t2, fail
	li a1, MAGIC
	j shutdown
trap_handler:
	csrr t0, sepc
	bne t0, s0, fail
	csrr t0, scause
	bne t0, s1, fail
	csrr t0, sstatus
	andi t0, t0, 0x100
	bne t0, s3, fail
	csrw sepc, s2
	li t0, 0x100
	csrs sstatus, t0
	sret
fail:
	li a1, 0x4446414900
	or a1, a1, s5 /* distinct stage number, never a success marker */
shutdown:
	li a7, 0x53525354
	li a6, 0
	li a0, 0
	ecall
1: j 1b
