/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Stage-D MMU payload (review-d3 structure).  Supervisor identity,
 * bare satp at entry.  Sv39 identity map: root[2]->L1, L1[1]->L2,
 * L2[i] maps VA 0x80200000+i*4096 to the same PA, V|R|W|X|A|D=0xcf
 * (U=0), covering code/handler/tables.  TESTPG=0x803fa000 = L2[506];
 * its PTE is manipulated per test with sfence after each change.
 *
 * Assertions (bitmap in s11, exact scause AND stval required):
 *   M2  fresh A=0/D=0 PTE: load sets A=1, D stays 0
 *   M1  store sets D=1
 *   M3  U=1 + SUM=0: supervisor load faults cause=13 stval=TESTVA
 *   M4  U=1 + SUM=1: same load succeeds
 *   M5  U=1: kernel fetch faults cause=12 regardless of SUM
 *   M6  X-only U=0: MXR=0 load faults 13; MXR=1 succeeds
 * Recovery: the handler jumps to a recovery PC stored in a fixed slot
 * (never sepc+4).  .option norvc throughout.
 */
#define D2_MAGIC 0x50564D4B564D4D55
#define D2_FAIL 0x4D4D55464C4946
#define ROOT_GPA 0x803fb000
#define L1_GPA 0x803fd000
#define L2_GPA 0x803fc000
#define TESTPG 0x803fa000
#define TESTVA TESTPG
#define L2_SLOT_TEST 506
#define RECOV 0x803f9000
#define EXP_CAUSE (RECOV + 8)
#define EXP_TVAL (RECOV + 16)

	.option push
	.option norvc

	.globl _start
_start:
	/* zero the three table pages */
	li	t0, L2_GPA
	li	t1, 4096
1:	sd	zero, 0(t0)
	addi	t0, t0, 8
	addi	t1, t1, -8
	bgtz	t1, 1b
	li	t0, L1_GPA
	sd	zero, 0(t0)
	li	t0, ROOT_GPA
	sd	zero, 0(t0)

	/* root[2] -> L1 ; L1[1] -> L2 */
	li	t0, ROOT_GPA
	li	t1, (L1_GPA >> 12)
	slli	t1, t1, 10
	ori	t1, t1, 1
	sd	t1, 16(t0)
	li	t0, L1_GPA
	li	t1, (L2_GPA >> 12)
	slli	t1, t1, 10
	ori	t1, t1, 1
	sd	t1, 8(t0)

	/* L2[0..511]: identity V|R|W|X|A|D = 0xcf */
	li	t0, L2_GPA
	li	t1, 0x80200000
	li	t2, 512
2:	srli	t4, t1, 12
	slli	t4, t4, 10
	ori	t4, t4, 0xcf
	sd	t4, 0(t0)
	addi	t0, t0, 8
	li	t5, 4096
	add	t1, t1, t5
	addi	t2, t2, -1
	bgtz	t2, 2b

	/* install trap handler (bare: va==pa) */
	la	t0, mmu_trap
	csrw	stvec, t0

	/* enable Sv39 */
	li	t0, (8 << 60)
	li	t1, (ROOT_GPA >> 12)
	or	t0, t0, t1
	csrw	satp, t0
	sfence.vma

	li	s11, 0

	/* helper macro-ish: set TEST PTE from t2 flags */
	/* ---- M2: fresh PTE V|R|W|X (A=D=0), load sets A not D ---- */
	li	t0, L2_GPA
	li	t1, L2_SLOT_TEST
	slli	t1, t1, 3
	add	t0, t0, t1
	li	t2, (TESTPG >> 12)
	slli	t2, t2, 10
	ori	t2, t2, 0x0f
	sd	t2, 0(t0)
	sfence.vma
	li	t0, TESTVA
	ld	t1, 0(t0)
	/* re-read PTE */
	li	t0, L2_GPA
	li	t1, L2_SLOT_TEST
	slli	t1, t1, 3
	add	t0, t0, t1
	ld	t2, 0(t0)
	andi	t3, t2, 0x40
	beqz	t3, d2_fail			/* A must be 1 */
	andi	t3, t2, 0x80
	bnez	t3, d2_fail			/* D must be 0 */
	ori	s11, s11, 1

	/* ---- M1: store sets D ---- */
	li	t0, TESTVA
	li	t1, 0x5a5a
	sd	t1, 0(t0)
	li	t0, L2_GPA
	li	t1, L2_SLOT_TEST
	slli	t1, t1, 3
	add	t0, t0, t1
	ld	t2, 0(t0)
	andi	t3, t2, 0x80
	beqz	t3, d2_fail
	ori	s11, s11, 2

	/* ---- M3: U=1 SUM=0 -> load fault 13 ---- */
	li	t0, L2_GPA
	li	t1, L2_SLOT_TEST
	slli	t1, t1, 3
	add	t0, t0, t1
	ld	t2, 0(t0)
	ori	t2, t2, 0x10
	sd	t2, 0(t0)
	sfence.vma
	li	t4, (1 << 18)
	csrc	sstatus, t4
	la	t5, m3_done
	li	t6, RECOV
	sd	t5, 0(t6)
	li	t5, 13
	sd	t5, 8(t6)
	li	t5, TESTVA
	sd	t5, 16(t6)
	li	t0, TESTVA
	ld	t1, 0(t0)			/* MUST FAULT */
	j	d2_fail
m3_done:
	ori	s11, s11, 4

	/* ---- M4: U=1 SUM=1 -> succeeds ---- */
	csrs	sstatus, t4
	li	t0, TESTVA
	ld	t1, 0(t0)
	ori	s11, s11, 8

	/* ---- M5: U=1 -> kernel fetch faults 12 ---- */
	la	t5, m5_done
	li	t6, RECOV
	sd	t5, 0(t6)
	li	t5, 12
	sd	t5, 8(t6)
	li	t5, TESTVA
	sd	t5, 16(t6)
	li	t0, TESTVA
	jalr	ra, 0(t0)			/* MUST FAULT */
	j	d2_fail
m5_done:
	ori	s11, s11, 16

	/* ---- M6: X-only U=0, MXR ---- */
	li	t0, L2_GPA
	li	t1, L2_SLOT_TEST
	slli	t1, t1, 3
	add	t0, t0, t1
	li	t2, (TESTPG >> 12)
	slli	t2, t2, 10
	ori	t2, t2, 0x09			/* V|X only */
	sd	t2, 0(t0)
	sfence.vma
	li	t4, (1 << 19)
	csrc	sstatus, t4			/* MXR=0 */
	la	t5, m6a_done
	li	t6, RECOV
	sd	t5, 0(t6)
	li	t5, 13
	sd	t5, 8(t6)
	li	t5, TESTVA
	sd	t5, 16(t6)
	li	t0, TESTVA
	ld	t1, 0(t0)			/* MUST FAULT */
	j	d2_fail
m6a_done:
	csrs	sstatus, t4			/* MXR=1 */
	li	t0, TESTVA
	ld	t1, 0(t0)			/* must succeed */
	ori	s11, s11, 32

	/* all 6 bits */
	li	t0, 0x3f
	and	t0, s11, t0
	li	t1, 0x3f
	bne	t0, t1, d2_fail

	li	a7, 0x53525354
	li	a6, 0
	li	a0, 0
	li	a1, D2_MAGIC
	ecall
1:	j	1b

d2_fail:
	li	a7, 0x53525354
	li	a6, 0
	li	a0, 0
	li	a1, D2_FAIL
	ecall
1:	j	1b

mmu_trap:
	csrr	t0, scause
	csrr	t1, stval
	li	t2, RECOV
	ld	t3, 8(t2)			/* expected cause */
	bne	t0, t3, d2_fail
	ld	t3, 16(t2)			/* expected tval */
	bne	t1, t3, d2_fail
	ld	t3, 0(t2)			/* recovery pc */
	jr	t3

	.option pop
