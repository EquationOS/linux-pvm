/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_RISCV_PVM_HYPERCALL_H
#define _ASM_RISCV_PVM_HYPERCALL_H

/*
 * Minimal-dependency PV hypercall inline helpers for use in early
 * headers (irqflags.h, tlbflush.h).  This file has NO includes and
 * NO external symbol references other than pvm_pv_mode_active.
 *
 * The EFI libstub compiles with -DNO_ALTERNATIVE and cannot reference
 * kernel .data symbols: all hooks guard on !NO_ALTERNATIVE so the
 * stub sees plain native instructions.
 */

#ifndef __ASSEMBLY__

#include <uapi/asm/pvm_para.h>

#ifndef NO_ALTERNATIVE
extern bool pvm_pv_mode_active;
#define PVM_HOOKS_ACTIVE (pvm_pv_mode_active)
#else
/* EFI libstub: always native, no kernel symbol references */
#define PVM_HOOKS_ACTIVE (false)
#endif

static __always_inline void pvm_pv_irq_save(unsigned long *flags)
{
	register unsigned long a0 asm("a0") = 0;
	register unsigned long a1 asm("a1") = 0;
	register unsigned long a6 asm("a6") = PVM_HC_PV_IRQ_SAVE;
	register unsigned long a7 asm("a7") = PVM_HC_EID;

	asm volatile ("ecall" : "+r"(a0), "+r"(a1)
		      : "r"(a6), "r"(a7) : "memory");
	*flags = a0;
}

static __always_inline void pvm_pv_irq_restore(unsigned long flags)
{
	register unsigned long a0 asm("a0") = flags;
	register unsigned long a1 asm("a1") = 0;
	register unsigned long a6 asm("a6") = PVM_HC_PV_IRQ_RESTORE;
	register unsigned long a7 asm("a7") = PVM_HC_EID;

	asm volatile ("ecall" : "+r"(a0), "+r"(a1)
		      : "r"(a6), "r"(a7) : "memory");
}

static __always_inline void pvm_pv_irq_disable(unsigned long *flags)
{
	register unsigned long a0 asm("a0") = 0;
	register unsigned long a1 asm("a1") = 0;
	register unsigned long a6 asm("a6") = PVM_HC_PV_IRQ_DISABLE;
	register unsigned long a7 asm("a7") = PVM_HC_EID;

	asm volatile ("ecall" : "+r"(a0), "+r"(a1)
		      : "r"(a6), "r"(a7) : "memory");
	*flags = a0;
}

static __always_inline void pvm_pv_load_pgtbl(unsigned long satp_val)
{
	register unsigned long a0 asm("a0") = 0;
	register unsigned long a1 asm("a1") = satp_val;
	register unsigned long a6 asm("a6") = PVM_HC_LOAD_PGTBL;
	register unsigned long a7 asm("a7") = PVM_HC_EID;

	asm volatile ("ecall" : "+r"(a0), "+r"(a1)
		      : "r"(a6), "r"(a7) : "memory");
}

static __always_inline void pvm_pv_tlb_flush_all(void)
{
	register unsigned long a0 asm("a0") = 0;
	register unsigned long a1 asm("a1") = 0;
	register unsigned long a6 asm("a6") = PVM_HC_TLB_FLUSH;
	register unsigned long a7 asm("a7") = PVM_HC_EID;

	asm volatile ("ecall" : "+r"(a0), "+r"(a1)
		      : "r"(a6), "r"(a7) : "memory");
}

static __always_inline void pvm_pv_tlb_flush_page(unsigned long addr)
{
	register unsigned long a0 asm("a0") = addr;
	register unsigned long a1 asm("a1") = 0;
	register unsigned long a6 asm("a6") = PVM_HC_TLB_INVLPG;
	register unsigned long a7 asm("a7") = PVM_HC_EID;

	asm volatile ("ecall" : "+r"(a0), "+r"(a1)
		      : "r"(a6), "r"(a7) : "memory");
}

#endif /* __ASSEMBLY__ */
#endif /* _ASM_RISCV_PVM_HYPERCALL_H */
