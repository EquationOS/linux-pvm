/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * PVM event entry for RV64.
 *
 * The host enters pvm_event_entry to inject an event into the guest
 * kernel with:
 *
 *	a0	pointer to the host-provided register frame holding the
 *		interrupted context (struct pt_regs layout, read-only
 *		until PVM_HC_RETU hands it back)
 *	a1	event cause (raw scause-style value)
 *	sp	a valid, 16-byte aligned guest kernel stack
 *
 * General purpose registers of the interrupted context are already
 * captured in the frame; only the registers live across the C call are
 * saved here.  On return the (possibly updated) frame is handed back to
 * the host through PVM_HC_RETU in a0, the ERETU analog.
 *
 * This code can only run once a PVM host has accepted registration; a
 * native S-mode boot never executes it.
 */
#include <linux/linkage.h>

#include <asm/asm.h>

#include <asm/pvm_para.h>

	.text

SYM_CODE_START(pvm_event_entry)
	/* Keep the stack 16-byte aligned across the frame. */
	addi	sp, sp, -16
	sd	ra, 0(sp)

	mv	a2, a0		/* frame */
	mv	a0, a1		/* cause */
	mv	a1, a2
	call	pvm_event_dispatch

	ld	ra, 0(sp)
	addi	sp, sp, 16

	/* a0 holds the frame to resume; the host must not return here. */
	li	a6, PVM_HC_RETU
	li	a7, PVM_HC_EID
	ecall

	/* Should never be reached. */
	j	pvm_event_entry
SYM_CODE_END(pvm_event_entry)
