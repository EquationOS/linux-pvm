/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * PVM event entry for RV64.
 *
 * The host enters pvm_event_entry to inject an event into the guest
 * kernel with (see uapi/asm/pvm_para.h for the full contract):
 *
 *	a0	guest virtual address of the host-provided register
 *		frame holding the interrupted context (struct pt_regs
 *		layout, read-only until PVM_HC_RETU hands it back)
 *	a1	event cause (raw scause-style value)
 *	sp	a fresh, 16-byte aligned guest kernel stack
 *	tp/gp	the guest kernel values the host maintains for this
 *		vCPU; never the interrupted guest user TLS
 *
 * All other GPRs are undefined at entry: the interrupted context is
 * fully captured in the frame.  Callee-saved registers survive the C
 * call; nothing else needs preserving here.
 *
 * Fail-stop contract: pvm_event_dispatch() returns the frame to
 * resume in a0, or NULL when the event is fatal.  A PVM_HC_RETU that
 * returns at all is a host protocol violation.  Both cases park this
 * vCPU in a tight loop -- no further ecalls, no memory access through
 * a possibly bogus pointer -- instead of resuming or retrying.
 *
 * This code can only run once a PVM host has accepted registration; a
 * native S-mode boot never executes it.
 */
#include <linux/linkage.h>

#include <asm/asm.h>

#include <asm/pvm_para.h>

	.text

SYM_CODE_START(pvm_event_entry)
	addi	sp, sp, -16
	sd	ra, 0(sp)

	mv	a2, a0		/* frame */
	mv	a0, a1		/* cause */
	mv	a1, a2
	call	pvm_event_dispatch

	beqz	a0, pvm_event_fail

	ld	ra, 0(sp)
	addi	sp, sp, 16

	/* a0 holds the frame to resume; the host must not return here. */
	li	a6, PVM_HC_RETU
	li	a7, PVM_HC_EID
	ecall

	/* Host protocol violation: fail-stop. */
SYM_INNER_LABEL(pvm_event_fail, SYM_L_GLOBAL)
1:	j	1b
SYM_CODE_END(pvm_event_entry)
