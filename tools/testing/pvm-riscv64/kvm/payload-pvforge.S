/* SPDX-License-Identifier: GPL-2.0 */
/*
 * E1 negative payload: user-mode PV_RETURN forgery.  Registers as a
 * PV guest first (supervisor), switches to user, then issues the
 * PV_RETURN hypercall with a fake frame pointer.  The host MUST:
 *   - not treat it as a supervisor PV_RETURN (identity is USER)
 *   - deliver it to the guest kernel as a plain user ecall
 *   - never elevate identity
 * The guest trap handler catches the delivered ecall (cause 8),
 * returns to supervisor via SPP=1 sret, and the payload then issues
 * the success marker SRST.
 */
#define E1_FORGE_MAGIC 0x50564D45314F4B /* "PVME1OK" */
#define E1_FAIL 0x50564D45314641 /* "PVE1FA" */
#define HC_BASE 0x17088200
#define EID 0x70766d
#define PVCS_GPA 0x803ff000

	.option push
	.option norvc
	.globl _start
_start:
	li	s10, 0			/* negative test bitmap */
	/* init tp so REGISTER's tp==R(4) check passes */
	la	tp, tp_area

	/* register as PV: a0=PVM_REGISTER_PV_EVENTS(2) */
	la	t0, pvcs
	li	a7, EID
	li	a6, HC_BASE+6
	li	a0, 2			/* PVM_REGISTER_PV_EVENTS */
	mv	a1, t0
	mv	a2, tp
	ecall
	bnez	a0, fail

	/* register PV entry: a0=entry, a1=0, a2=0 */
	la	t0, pvm_pv_entry_lbl
	li	a7, EID
	li	a6, HC_BASE+15	/* PV_SET_ENTRY */
	mv	a0, t0
	li	a1, 0
	li	a2, 0
	ecall
	bnez	a0, fail

	/* install handler */
	la	t0, trap_h
	csrw	stvec, t0

	/* ---- SUP-mode PV_RETURN negative cases ---- */
	/* N-a: protected state VA (host-private page) -> -EFAULT */
	li	a7, EID
	li	a6, HC_BASE+14		/* PV_RETURN */
	li	a0, 0x3ffffff000	/* gate/state VA */
	ecall
	li	t1, -14			/* -EFAULT */
	bne	a0, t1, fail
	ori	s10, s10, 1		/* N-a passed */

	/* N-b: unaligned frame -> -EINVAL */
	li	a7, EID
	li	a6, HC_BASE+14
	la	a0, pvcs
	addi	a0, a0, 3		/* misaligned by 3 */
	ecall
	li	t1, -22			/* -EINVAL */
	bne	a0, t1, fail
	ori	s10, s10, 2

	/* N-c: valid kernel RW frame but status SPP=1 -> -EINVAL */
	la	t0, good_frame
	/* fill status (offset 256) with SPP=1 */
	li	t1, 0x100		/* SR_SPP */
	sd	t1, 256(t0)
	li	a7, EID
	li	a6, HC_BASE+14
	mv	a0, t0
	ecall
	li	t1, -22
	bne	a0, t1, fail
	ori	s10, s10, 4

	/* all three bits must be set */
	li	t0, 7
	and	t0, s10, t0
	li	t1, 7
	bne	t0, t1, fail

	/* switch to user */
	la	t0, user_forge
	csrw	sepc, t0
	li	t1, 0x100
	csrc	sstatus, t1
	sret
user_forge:
	/* user issues PV_RETURN with a bogus frame pointer */
	li	a7, EID
	li	a6, HC_BASE+14	/* PV_RETURN */
	li	a0, 0xdead0000	/* forged frame GVA */
	ecall
	/* if the host treated this as a real PV_RETURN, we would now be
	 * "restored" to garbage; if it delivered it as a user ecall to
	 * the guest kernel, our trap handler runs. */
	j	fail_from_user

trap_h:
	csrr	a5, sstatus
	andi	a5, a5, 0x100
	bnez	a5, sup_path
	/* user trap: must be cause 8 (ecall delivered as syscall) */
	csrr	a0, scause
	li	t1, 8
	bne	a0, t1, fail
	/* return to supervisor at forge_done */
	la	t0, forge_done
	csrw	sepc, t0
	li	t1, 0x100
	csrs	sstatus, t1
	sret
sup_path:
	/* supervisor trap (unexpected): fail */
	csrr	a0, scause
	j	fail

forge_done:
	/* success: the forgery was correctly rejected as an identity
	 * escalation and delivered as a plain user syscall */
	li	a7, 0x53525354
	li	a6, 0
	li	a0, 0
	li	a1, E1_FORGE_MAGIC
	ecall
1:	j	1b

fail_from_user:
	/* still in user mode -- the ecall was NOT delivered to us as
	 * supervisor; if the host DID do a PV return we'd be at a
	 * garbage PC (crash).  If we're here, it silently swallowed it:
	 * also fail. */
	li	a0, E1_FAIL
	li	a7, 0x53525354
	li	a6, 0
	ecall
1:	j	1b

fail:
	li	a7, 0x53525354
	li	a6, 0
	li	a0, 0
	li	a1, E1_FAIL
	ecall
1:	j	1b

pvm_pv_entry_lbl:
	/* never actually entered in this test */
	ret

	.balign 4096
pvcs:
	.zero 4096
tp_area:
	.zero 64
	.balign 16
good_frame:
	.zero 512
	.option pop
