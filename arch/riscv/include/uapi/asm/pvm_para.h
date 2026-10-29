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

/* PVM_HC_LOAD_PGTBL flags */
#define PVM_LOAD_PGTBL_FLAGS_ASID	_BITUL(0)
#define PVM_LOAD_PGTBL_FLAGS_SV57	_BITUL(1)

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
