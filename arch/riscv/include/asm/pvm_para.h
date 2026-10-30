/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_RISCV_PVM_PARA_H
#define _ASM_RISCV_PVM_PARA_H

#include <linux/init.h>
#include <uapi/asm/pvm_para.h>

#ifndef __ASSEMBLY__

#ifdef CONFIG_PVM_GUEST

extern bool pvm_guest_detected;

void __init pvm_early_setup(void);

/* arch/riscv/kernel/pvm.c */
void pvm_safe_halt(void);
void pvm_flush_tlb_all(void);
void pvm_flush_tlb_current(void);
void pvm_flush_tlb_page(unsigned long addr);
void pvm_load_pgtbl(unsigned long satp);
struct pt_regs;
__visible struct pt_regs *pvm_event_dispatch(unsigned long cause,
					     struct pt_regs *regs);

/* arch/riscv/kernel/pvm-entry.S */
void pvm_event_entry(void);

/*
 * pvm_hypercallN() must only be called after pvm_early_setup() has detected
 * a PVM host, otherwise the ecall traps into SBI/OpenSBI.
 */
static __always_inline long pvm_hypercall0(unsigned long nr)
{
	register unsigned long a0 asm ("a0");
	register unsigned long a1 asm ("a1");
	register unsigned long a6 asm ("a6") = nr;
	register unsigned long a7 asm ("a7") = PVM_HC_EID;

	asm volatile ("ecall"
		      : "=r" (a0), "=r" (a1)
		      : "r" (a6), "r" (a7)
		      : "memory");
	return a0;
}

static __always_inline long pvm_hypercall1(unsigned long nr, unsigned long arg0)
{
	register unsigned long a0 asm ("a0") = arg0;
	register unsigned long a1 asm ("a1");
	register unsigned long a6 asm ("a6") = nr;
	register unsigned long a7 asm ("a7") = PVM_HC_EID;

	asm volatile ("ecall"
		      : "+r" (a0), "=r" (a1)
		      : "r" (a6), "r" (a7)
		      : "memory");
	return a0;
}

static __always_inline long pvm_hypercall2(unsigned long nr, unsigned long arg0,
					   unsigned long arg1)
{
	register unsigned long a0 asm ("a0") = arg0;
	register unsigned long a1 asm ("a1") = arg1;
	register unsigned long a6 asm ("a6") = nr;
	register unsigned long a7 asm ("a7") = PVM_HC_EID;

	asm volatile ("ecall"
		      : "+r" (a0), "+r" (a1)
		      : "r" (a6), "r" (a7)
		      : "memory");
	return a0;
}

static __always_inline long pvm_hypercall3(unsigned long nr, unsigned long arg0,
					   unsigned long arg1, unsigned long arg2)
{
	register unsigned long a0 asm ("a0") = arg0;
	register unsigned long a1 asm ("a1") = arg1;
	register unsigned long a2 asm ("a2") = arg2;
	register unsigned long a6 asm ("a6") = nr;
	register unsigned long a7 asm ("a7") = PVM_HC_EID;

	asm volatile ("ecall"
		      : "+r" (a0), "+r" (a1)
		      : "r" (a2), "r" (a6), "r" (a7)
		      : "memory");
	return a0;
}

#else /* CONFIG_PVM_GUEST */

static inline void pvm_early_setup(void)
{
}

#endif /* CONFIG_PVM_GUEST */

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_RISCV_PVM_PARA_H */
