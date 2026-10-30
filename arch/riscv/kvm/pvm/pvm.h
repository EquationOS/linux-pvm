/* SPDX-License-Identifier: GPL-2.0 */
#ifndef RISCV_KVM_PVM_H
#define RISCV_KVM_PVM_H
#include <linux/kvm_host.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/sched/signal.h>
#include <asm/timex.h>
#include <asm/io.h>
#define PVM_MXR BIT(19)
#include <asm/csr.h>
#include <asm/pvm_para.h>
#include "switch.h"
#define PVM_RAM_BASE 0x80200000UL
#define PVM_RAM_END 0x88000000UL
#define PVM_POOL_PAGES 256
struct riscv_pvm_vm {
	struct page **pages;
	unsigned long nr_pages;
	atomic_t invalid;
	bool bound;
};
struct pvm_root {
	unsigned long **tables;
	unsigned int used;
	unsigned long *root;
};
struct riscv_pvm_vcpu {
	struct kvm_vcpu *vcpu;
	struct pvm_switch_state *s;
	void *gate;
	struct pvm_root bridge, kernel, user;
	unsigned long csr[256], satp, deadline;
	unsigned long pvcs, kernel_tp;
	bool supervisor, registered, sbi_pending, halted;
	u64 exits, faults, user_entries, timer_irqs, schedules, switches;
};
int pvm_mmu_init(struct riscv_pvm_vcpu *v);
void pvm_mmu_destroy(struct riscv_pvm_vcpu *v);
int pvm_bind_memory(struct kvm_vcpu *vcpu);
void pvm_reset_roots(struct riscv_pvm_vcpu *v);
unsigned long pvm_real_satp(struct riscv_pvm_vcpu *v);
void *pvm_gpa(struct riscv_pvm_vcpu *v, unsigned long gpa, unsigned long size);
unsigned long pvm_translate(struct riscv_pvm_vcpu *v, unsigned long va,
			    unsigned long *pte);
int pvm_page_fault(struct riscv_pvm_vcpu *v, unsigned long cause,
		   unsigned long va);
int pvm_emulate(struct riscv_pvm_vcpu *v);
void pvm_inject(struct riscv_pvm_vcpu *v, unsigned long cause,
		unsigned long val);
#endif
