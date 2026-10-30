/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_RISCV_PVM_PARA_H
#define _UAPI_ASM_RISCV_PVM_PARA_H

#include <linux/const.h>

/*
 * PVM (Pagetable-based Virtual Machine) guest support for RISC-V.
 *
 * On x86_64 a PVM guest kernel runs in CPL3 and communicates with the host
 * through patched KVM hypercalls, see Documentation/virt/kvm/x86/pvm-spec.rst
 * and arch/x86/include/uapi/asm/pvm_para.h for the original ABI.
 *
 * On RV64 the equivalent guest kernel runs in U-mode under the host kernel
 * and uses a dedicated ecall channel with the following calling convention
 * (register usage deliberately mirrors the SBI calling convention so that a
 * future host can dispatch it from an SBI-style trap handler):
 *
 *	a7	hypercall extension id, always PVM_HC_EID
 *	a6	hypercall number (PVM_HC_*)
 *	a0-a5	up to 6 arguments
 *	a0	return value: 0 on success, negative errno on failure
 *	a1	optional second return value
 *
 * PVM hypercalls must never be issued when the kernel runs natively (S-mode
 * under OpenSBI, or real hardware): the ecall would trap into the firmware.
 * All PVM guest code paths are therefore gated behind the device-tree based
 * detection performed in arch/riscv/kernel/pvm.c.
 */
#define PVM_HC_EID			0x70766D /* "pvm" */

/*
 * Hypercall numbers below keep the arch-neutral part of the x86_64 numbering
 * (see arch/x86/include/uapi/asm/pvm_para.h).  The x86-only entries (segment
 * and MSR operations: LOAD_GS, RDMSR, WRMSR, LOAD_TLS) have no RV64
 * counterpart and are not reused.
 */
#define PVM_HC_SPECIAL_MAX_NR		(256)
#define PVM_HC_SPECIAL_BASE		(0x17088200)
#define PVM_HC_SPECIAL_MAX		(PVM_HC_SPECIAL_BASE+PVM_HC_SPECIAL_MAX_NR)

#define PVM_HC_LOAD_PGTBL		(PVM_HC_SPECIAL_BASE+0)
#define PVM_HC_IRQ_WIN			(PVM_HC_SPECIAL_BASE+1)
#define PVM_HC_IRQ_HALT			(PVM_HC_SPECIAL_BASE+2)
#define PVM_HC_TLB_FLUSH		(PVM_HC_SPECIAL_BASE+3)
#define PVM_HC_TLB_FLUSH_CURRENT	(PVM_HC_SPECIAL_BASE+4)
#define PVM_HC_TLB_INVLPG		(PVM_HC_SPECIAL_BASE+5)
#define PVM_HC_REGISTER_PVCS		(PVM_HC_SPECIAL_BASE+6)
#define PVM_HC_SET_EVENT_ENTRY		(PVM_HC_SPECIAL_BASE+7)
#define PVM_HC_RETU			(PVM_HC_SPECIAL_BASE+8)
#define PVM_HC_PROBE			(PVM_HC_SPECIAL_BASE+9)
#define PVM_HC_DEREGISTER		(PVM_HC_SPECIAL_BASE+10)

/*
 * PVM_HC_LOAD_PGTBL flags are reserved and must be zero; the host decodes
 * the raw satp value itself (MODE, ASID and PPN fields per the Sv48/Sv57
 * layout) so that flush/ASID policy stays on the host side.
 */

/*
 * Guest-side ABI version for the RV64 PVM host protocol.  The guest
 * probes with the version it was built for; the host answers with the
 * version it supports (a guest must refuse to run on a lower one).
 */
#define PVM_ABI_VERSION			1

/*
 * Address types used by the registration hypercalls:
 *
 *	HPA	host physical, only meaningful inside the host
 *	GPA	"guest physical": an address in the host-granted backing
 *		of this guest; the host translates GPA->HPA through its
 *		 own mapping records, the guest never sees HPAs
 *	GVA	guest virtual address in the guest kernel address space
 */

/*
 * PVM_HC_PROBE performs the version/capability handshake:
 *	arg0	the PVM_ABI_VERSION the guest was built for
 *	returns	the highest host-supported PVM_ABI_VERSION, or a
 *		negative errno when no PVM host is present (under
 *		OpenSBI this is -2, SBI_ERR_NOT_SUPPORTED)
 *
 * PVM_HC_REGISTER_PVCS binds this vCPU to its communication state:
 *	arg0	flags, reserved, must be zero
 *	arg1	GPA of this CPU's struct pvm_vcpu_struct
 *	arg2	the initial guest-kernel tp value for this vCPU (on
 *		RV64 Linux tp holds current).  The host installs it
 *		when first entering the guest kernel context and then
 *		saves/restores the guest kernel tp and gp it observes;
 *		it must never pass through the interrupted guest user
 *		TLS tp
 *
 * PVM_HC_SET_EVENT_ENTRY installs the event entry point:
 *	arg0	flags, reserved, must be zero
 *	arg1	GVA of pvm_event_entry
 *
 * PVM_HC_DEREGISTER rolls back a partially or fully completed
 * registration of this vCPU (PVCS binding, TP and event entry).
 *
 * PVM_HC_RETU asks the host to resume the context described by the frame
 * passed in arg0 (the ERETU analog).  Only valid after a successful
 * REGISTER_PVCS; a conforming host must never let it return.
 */

/*
 * Event injection contract (what pvm_event_entry may rely on):
 *
 *	a0	GVA of the register frame (struct pt_regs layout) holding
 *		the complete interrupted context; read-only for the
 *		guest until the frame is handed back via PVM_HC_RETU
 *	a1	event cause, raw scause-style value
 *	sp	a fresh guest kernel stack top, 16-byte aligned
 *	tp/gp	the guest kernel values the host maintains for this
 *		vCPU (initialized from PVM_HC_REGISTER_PVCS arg2 for tp;
 *		gp matters under shadow call stacks); never the
 *		interrupted guest user TLS tp
 *
 * All other GPRs are undefined at entry; the interrupted context is
 * fully captured in the frame.  FPU/vector state belongs to the
 * interrupted context: the host preserves it with the frame, and
 * event entry and dispatch code must not touch it.  Events are
 * delivered with virtual interrupts inhibited (event_flags.SIE = 0)
 * and the host keeps them inhibited until the matching PVM_HC_RETU.
 */

/*
 * PVM_EVENT_FLAGS_SIE
 *	- Supervisor interrupt enable flag, the sstatus.SIE analog.  Set to
 *	  allow the host to deliver external interrupt events, cleared to
 *	  inhibit them.
 *
 * PVM_EVENT_FLAGS_SIP
 *	- Interrupt pending flag.  The host sets it when it cannot inject an
 *	  external interrupt event because SIE is cleared.
 */
#define PVM_EVENT_FLAGS_SIP_BIT		8
#define PVM_EVENT_FLAGS_SIP		_BITUL(PVM_EVENT_FLAGS_SIP_BIT)
#define PVM_EVENT_FLAGS_SIE_BIT		9
#define PVM_EVENT_FLAGS_SIE		_BITUL(PVM_EVENT_FLAGS_SIE_BIT)

#ifndef __ASSEMBLY__

/*
 * Per-VCPU communication structure (PVCS).  The guest registers its physical
 * address with the host (x86 does this through MSR_PVM_VCPU_STRUCT; on RV64
 * the registration hypercall will be defined together with the host side).
 *
 * For an event from user mode the host saves the interrupted user context
 * here and the synthetic return-to-user (the ERETU analog) restores from it.
 * General purpose registers are saved by the guest event entry code onto the
 * kernel stack, only the state that cannot be reconstructed is kept here.
 */
struct pvm_vcpu_struct {
	/*
	 * Only used in supervisor (guest kernel) mode, only bits 8 and 9
	 * (PVM_EVENT_FLAGS_*) are valid, other bits are reserved.
	 */
	__u64 event_flags;

	/* Fault address (stval) of the event being delivered, cr2 analog. */
	__u64 stval;

	__u64 reserved0[6];

	/* Interrupted user context for the return-to-user path. */
	__u64 sepc;
	__u64 sstatus;
	__u64 reserved1[6];
};

#endif /* __ASSEMBLY__ */

#endif /* _UAPI_ASM_RISCV_PVM_PARA_H */
