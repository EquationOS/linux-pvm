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
	/* Host-owned task context, learned only from validated PV_RETURN. */
	unsigned long kernel_gp, pv_entry_gva, pv_frame_gva, pv_frame_sp;
	void *pv_frame_kva[2];
	size_t pv_frame_len[2];
	bool pv_context_valid;
	bool supervisor, registered, sbi_pending, halted, pv_mode;

	u64 exits, faults, user_entries, timer_irqs, schedules, switches;
	/* D2 classification counters (host-private, reset by
	 * PVM_HC_STATS_WINDOW): */
	u64 n_user_ecall, n_sbi, n_hypercall, n_csr, n_sret, n_sfence;
	u64 n_satp_write, n_fill, n_inj_fault, n_inj_irq, n_host_irq;
	u64 n_vmm_exit, n_wfi, n_inj_ill;
	u64 n_pv_return, n_pv_reject, n_pv_entry;
	/* window bookkeeping: bases for EVERY exported counter */
	u64 win_active;
	u64 w_user_ecall, w_sbi, w_hypercall, w_csr, w_sret, w_sfence;
	u64 w_satp_write, w_fill, w_inj_fault, w_inj_irq, w_host_irq;
	u64 w_vmm_exit, w_wfi, w_inj_ill, w_exits;
	u64 w_pv_entry, w_pv_return, w_pv_reject;
	u32 pv_diag, pv_irq_diag, pv_flow_diag;
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
