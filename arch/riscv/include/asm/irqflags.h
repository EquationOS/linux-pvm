/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */


#ifndef _ASM_RISCV_IRQFLAGS_H
#define _ASM_RISCV_IRQFLAGS_H

#include <asm/csr.h>
#ifdef CONFIG_PVM_GUEST
#include <asm/pvm_hypercall.h>
/* E1: when pvm_pv_mode_active, IRQ enable/disable goes through
 * dedicated PV hypercalls that operate the host's virtual SIE.
 * Native and CSR-trap modes keep the raw CSR instructions below. */
#define PVM_IRQ_HOOKED PVM_HOOKS_ACTIVE
#endif

/* read interrupt enabled status */
static inline unsigned long arch_local_save_flags(void)
{
#ifdef CONFIG_PVM_GUEST
	unsigned long f;
	if (PVM_IRQ_HOOKED) {
		pvm_pv_irq_save(&f);
		return f;
	}
#endif
	return csr_read(CSR_STATUS);
}

/* unconditionally enable interrupts */
static inline void arch_local_irq_enable(void)
{
#ifdef CONFIG_PVM_GUEST
	if (PVM_IRQ_HOOKED) {
		pvm_pv_irq_restore(SR_IE);
		return;
	}
#endif
	csr_set(CSR_STATUS, SR_IE);
}

/* unconditionally disable interrupts */
static inline void arch_local_irq_disable(void)
{
#ifdef CONFIG_PVM_GUEST
	if (PVM_IRQ_HOOKED) {
		unsigned long f;
		pvm_pv_irq_disable(&f);
		return;
	}
#endif
	csr_clear(CSR_STATUS, SR_IE);
}

/* get status and disable interrupts (atomic read+disable) */
static inline unsigned long arch_local_irq_save(void)
{
#ifdef CONFIG_PVM_GUEST
	unsigned long f;
	if (PVM_IRQ_HOOKED) {
		pvm_pv_irq_disable(&f); /* returns old, clears SIE */
		return f;
	}
#endif
	return csr_read_clear(CSR_STATUS, SR_IE);
}

/* test flags */
static inline int arch_irqs_disabled_flags(unsigned long flags)
{
	return !(flags & SR_IE);
}

/* test hardware interrupt enable bit */
static inline int arch_irqs_disabled(void)
{
	return arch_irqs_disabled_flags(arch_local_save_flags());
}

/* set interrupt enabled status */
static inline void arch_local_irq_restore(unsigned long flags)
{
#ifdef CONFIG_PVM_GUEST
	if (PVM_IRQ_HOOKED) {
		pvm_pv_irq_restore(flags & SR_IE);
		return;
	}
#endif
	csr_set(CSR_STATUS, flags & SR_IE);
}

#endif /* _ASM_RISCV_IRQFLAGS_H */
