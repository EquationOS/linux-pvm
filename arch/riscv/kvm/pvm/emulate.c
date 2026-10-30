// SPDX-License-Identifier: GPL-2.0
#include "pvm.h"
#include <asm/ptrace.h>
#include <asm/pgtable-bits.h>
#include <linux/moduleparam.h>
static bool pv_debug;
module_param(pv_debug, bool, 0644);
#define R(i) v->s->g[i]
void pvm_inject(struct riscv_pvm_vcpu *v, unsigned long cause,
		unsigned long val)
{
	/* Central injection accounting (single site): cause 2 = illegal
	 * (supervisor AND user), cause 8 = user ecall event, async
	 * bit+cause = virtual IRQ.  Callers must NOT double-increment. */
	if (cause == 2)
		v->n_inj_ill++;
	else if (cause == 8)
		v->n_user_ecall++;
	else if (cause == 12 || cause == 13 || cause == 15)
		v->n_inj_fault++;
	else if (cause >> 63)
		v->n_inj_irq++;
	unsigned long s = v->csr[0], vec = v->csr[5];
	v->csr[0x41] = R(32);
	v->csr[0x42] = cause;
	v->csr[0x43] = val;
	v->csr[0] = (s & ~(SR_SPP | SR_SPIE | SR_SIE)) |
		    (v->supervisor ? SR_SPP : 0) | ((s & SR_SIE) ? SR_SPIE : 0);
	v->supervisor = true;
	R(32) = (vec & ~3UL) +
		((vec & 1) && (cause >> 63) ? 4 * (cause & 63) : 0);
}
static int readcsr(struct riscv_pvm_vcpu *v, unsigned long c,
		   unsigned long *val)
{
	if (c == CSR_SATP) {
		*val = v->satp;
		return 0;
	}
	if (c == CSR_TIME) {
		*val = get_cycles64();
		return 0;
	}
	if (c == CSR_CYCLE) {
		*val = csr_read(CSR_CYCLE);
		return 0;
	}
	if (c == CSR_INSTRET) {
		*val = csr_read(CSR_INSTRET);
		return 0;
	}
	switch (c) {
	case CSR_SSTATUS:
	case CSR_SIE:
	case CSR_STVEC:
	case CSR_SCOUNTEREN:
	case CSR_SENVCFG:
	case CSR_SSCRATCH:
	case CSR_SEPC:
	case CSR_SCAUSE:
	case CSR_STVAL:
	case CSR_SIP:
		*val = v->csr[c - 0x100];
		if (c == CSR_SSTATUS) {
			/* UXL fixed to RXL (64): bits [33:32]=2 */
			*val |= (2UL << 32);
			if ((R(33) & SR_FS) == SR_FS_DIRTY)
				*val |= SR_FS_DIRTY;
			if ((*val & SR_FS) == SR_FS_DIRTY)
				*val |= SR_SD;
		}
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}
static int writecsr(struct riscv_pvm_vcpu *v, unsigned long c,
		    unsigned long val)
{
	unsigned long old;
	if (c == CSR_SATP) {
		v->n_satp_write++;
		if ((val >> 60) != 0 && (val >> 60) != 8)
			return 0;
		v->satp = val;
		pvm_reset_roots(v);
		return 0;
	}
	if (readcsr(v, c, &old))
		return -EOPNOTSUPP;
	if (c < 0x100 || c >= 0x200)
		return -EOPNOTSUPP;
	if (c == CSR_SSTATUS) {
		/* WARL: guest controls only the architectural subset
		 * (SIE/SPIE/SPP/SUM/MXR/FS).  FS is preserved EXACTLY as
		 * written -- FS=Off is a legal Linux semantic (kernel
		 * disables FPU to trap first use); we must not force
		 * Dirty.  UXL is fixed to RXL (64); SD is derived at
		 * read time, never stored. */
		static const unsigned long m = SR_SIE | SR_SPIE | SR_SPP |
					       SR_SUM | PVM_MXR | SR_FS;
		val &= m;
	}
	if (c == CSR_STVEC) {
		/* WARL: base must be 4-byte aligned; MODE=0 (direct) or
		 * 1 (vectored) preserved; MODE=2/3 -> 0. */
		unsigned long mode = val & 3;
		if (mode > 1)
			val &= ~3UL;
	}
	if (c == CSR_SEPC) /* WARL: bit 0 always 0 (2-byte aligned) */
		val &= ~1UL;
	if (c == CSR_SIE) /* WARL: only SSIE+STIE implemented */
		val &= (0x2 | 0x20);
	if (c == CSR_SIP)
		/* SSIP guest-writable; STIP host-owned (timer), guest
		 * writes to it are ignored. */
		val = (v->csr[0x44] & 0x20) | (val & 0x2);
	if (c == CSR_SCOUNTEREN) /* only CY/TM/IR bits supported */
		val &= 7;
	if (c == CSR_SENVCFG) /* no envcfg features implemented: RAZ/WI */
		val = 0;
	v->csr[c - 0x100] = val;
	if (c == CSR_SSTATUS && ((old ^ val) & (SR_SUM | PVM_MXR)))
		pvm_reset_roots(v);
	return 0;
}
/* A frame always belongs to guest RAM; no guest address is dereferenced
 * as a host pointer. Validate each segment before copying either way. */
struct pv_frame_map {
	void *kva[2];
	size_t len[2];
};
static int pv_map_frame(struct riscv_pvm_vcpu *v, unsigned long va,
			struct pv_frame_map *m)
{
	size_t left = sizeof(struct pt_regs), done = 0;
	int i;

	BUILD_BUG_ON(sizeof(struct pt_regs) != 288);
	BUILD_BUG_ON(offsetof(struct pt_regs, ra) != sizeof(unsigned long));
	BUILD_BUG_ON(offsetof(struct pt_regs, status) != 32 * sizeof(unsigned long));
	memset(m, 0, sizeof(*m));
	if ((va & 15) || va > ULONG_MAX - left)
		return -EINVAL;
	for (i = 0; left && i < 2; i++) {
		unsigned long pte, gpa = pvm_translate(v, va + done, &pte);
		size_t len = min_t(size_t, left,
				    PAGE_SIZE - offset_in_page(va + done));

		if (!gpa || (pte & _PAGE_USER) ||
		    (pte & (_PAGE_READ | _PAGE_WRITE)) !=
			(_PAGE_READ | _PAGE_WRITE))
			return -EFAULT;
		m->kva[i] = pvm_gpa(v, gpa, len);
		if (!m->kva[i])
			return -EFAULT;
		m->len[i] = len;
		done += len;
		left -= len;
	}
	return left ? -EFAULT : 0;
}
static void pv_copy_frame(struct pv_frame_map *m, struct pt_regs *fr,
			  bool to_guest)
{
	size_t off = 0;
	int i;

	for (i = 0; i < 2; i++) {
		if (to_guest)
			memcpy(m->kva[i], (char *)fr + off, m->len[i]);
		else
			memcpy((char *)fr + off, m->kva[i], m->len[i]);
		off += m->len[i];
		if (off == sizeof(*fr))
			break;
	}
}
static void pv_set_status(struct riscv_pvm_vcpu *v, unsigned long st)
{
	unsigned long mask = SR_SIE | SR_SPIE | SR_SPP | SR_SUM | PVM_MXR | SR_FS;
	unsigned long old = v->csr[0];

	v->csr[0] = st & mask;
	if ((old ^ st) & (SR_SUM | PVM_MXR))
		pvm_reset_roots(v);
}
static int pv_return_user(struct riscv_pvm_vcpu *v, unsigned long fva)
{
	struct pv_frame_map m;
	struct pt_regs fr;
	unsigned long st;
	int ret;

	if (!v->registered || !v->pv_mode || !v->pv_entry_gva)
		return -EPERM;
	ret = pv_map_frame(v, fva, &m);
	if (ret)
		return ret;
	pv_copy_frame(&m, &fr, false);
	if (fr.status & SR_SPP)
		return -EINVAL;
	if (pv_debug && (v->pv_flow_diag++ < 128 || !fr.epc))
		pr_info("PVM_PV_RETURN frame=%lx ktp=%lx pc=%lx ra=%lx sp=%lx a7=%lx st=%lx\n",
			fva, R(4), fr.epc, fr.ra, fr.sp, fr.a7, fr.status);

	/* Capture kernel registers BEFORE replacing them with user state.
	 * The frame is the current task's real kernel-stack pt_regs. */
	v->kernel_tp = R(4);
	v->kernel_gp = R(3);
	v->pv_frame_gva = fva;
	v->pv_frame_sp = fva;
	v->pv_frame_kva[0] = m.kva[0];
	v->pv_frame_kva[1] = m.kva[1];
	v->pv_frame_len[0] = m.len[0];
	v->pv_frame_len[1] = m.len[1];
	v->pv_context_valid = true;
	st = (fr.status & ~(SR_SIE | SR_SPP)) | SR_SPIE;
	if (fr.status & SR_SPIE)
		st |= SR_SIE;
	pv_set_status(v, st);
	v->csr[CSR_SSCRATCH - 0x100] = v->kernel_tp;
	v->csr[CSR_SEPC - 0x100] = fr.epc & ~1UL;
	memcpy(&v->s->g[1], &fr.ra, 31 * sizeof(unsigned long));
	R(32) = fr.epc & ~1UL;
	v->supervisor = false;
	v->user_entries++;
	v->n_pv_return++;
	return 0;
}
static bool pv_enter_syscall(struct riscv_pvm_vcpu *v)
{
	struct pv_frame_map m;
	struct pt_regs fr;
	unsigned long st, pte, gpa;

	if (!v->registered || !v->pv_mode || !v->pv_entry_gva ||
	    !v->pv_context_valid)
		return false;
	/* Re-walk in the C path, including after guest address-space changes.
	 * E2 may cache this translation, with explicit invalidation rules. */
	gpa = pvm_translate(v, v->pv_entry_gva, &pte);
	if (!gpa || (pte & _PAGE_USER) || !(pte & _PAGE_EXEC) ||
	    !pvm_gpa(v, gpa, 2) || pv_map_frame(v, v->pv_frame_gva, &m))
		return false;

	memset(&fr, 0, sizeof(fr));
	fr.epc = R(32);
	memcpy(&fr.ra, &v->s->g[1], 31 * sizeof(unsigned long));
	readcsr(v, CSR_SSTATUS, &st);
	fr.status = (st & ~(SR_SPP | SR_SIE | SR_SPIE)) |
		    ((st & SR_SIE) ? SR_SPIE : 0);
	fr.cause = 8;
	fr.orig_a0 = R(10);
	if (pv_debug && (v->pv_flow_diag++ < 128 || !fr.epc))
		pr_info("PVM_PV_ENTER frame=%lx ktp=%lx pc=%lx ra=%lx sp=%lx a7=%lx st=%lx\n",
			v->pv_frame_gva, v->kernel_tp, fr.epc, fr.ra, fr.sp, fr.a7, fr.status);
	pv_copy_frame(&m, &fr, true);

	v->csr[CSR_SEPC - 0x100] = fr.epc;
	v->csr[CSR_SCAUSE - 0x100] = 8;
	v->csr[CSR_STVAL - 0x100] = 0;
	v->csr[CSR_SSCRATCH - 0x100] = 0;
	/* Match the native entry's disabling of SUM and FP before kernel C. */
	pv_set_status(v, fr.status & ~(SR_SUM | SR_FS));
	R(2) = v->pv_frame_sp;
	R(3) = v->kernel_gp;
	R(4) = v->kernel_tp;
	R(10) = v->pv_frame_gva;
	R(32) = v->pv_entry_gva;
	v->supervisor = true;
	v->n_pv_entry++;
	return true;
}

static void pv_irq_trace(struct riscv_pvm_vcpu *v)
{
	if (pv_debug && v->pv_diag++ < 16)
		pr_info("PVM_PV_IRQ pc=%lx ra=%lx fid=%lx virtual_s=%d sie=%lx\n",
			R(32), R(1), R(16), v->supervisor, v->csr[0] & SR_SIE);
}

static int hypercall(struct riscv_pvm_vcpu *v)
{
	long ret = -ENOSYS;
	unsigned long pte;
	switch (R(16)) {
	case PVM_HC_PROBE:
		ret = R(10) == PVM_ABI_VERSION ? PVM_ABI_VERSION : -EINVAL;
		break;
	case PVM_HC_GET_FEATURES:
		ret = PVM_FEATURE_CSR_TRAPS | PVM_FEATURE_PV_EVENTS;
		break;
	case PVM_HC_REGISTER_PVCS:
		if (v->registered)
			ret = -EBUSY;
		else if ((R(10) != PVM_REGISTER_CSR_EVENTS && R(10) != PVM_REGISTER_PV_EVENTS) || (R(11) & 4095) ||
			 !pvm_gpa(v, R(11), 4096) || R(12) != R(4) ||
			 !pvm_translate(v, R(12), &pte) || (pte & 0x10))
			ret = -EINVAL;
		else {
			v->pvcs = R(11);
			v->kernel_tp = R(12);
			v->registered = true;
			v->pv_mode = R(10) == PVM_REGISTER_PV_EVENTS;
			ret = 0;
		}
		break;
	case PVM_HC_DEREGISTER:
		v->registered = false;
		v->pv_mode = v->pv_context_valid = false;
		v->pv_entry_gva = 0;
		v->pvcs = v->kernel_tp = 0;
		ret = 0;
		break;
	case PVM_HC_LOAD_PGTBL:
		if (v->registered && !R(10))
			ret = writecsr(v, CSR_SATP, R(11));
		else
			ret = -EINVAL;
		break;
	case PVM_HC_TLB_FLUSH:
	case PVM_HC_TLB_FLUSH_CURRENT:
	case PVM_HC_TLB_INVLPG:
		if (v->registered) {
			pvm_reset_roots(v);
			ret = 0;
		} else
			ret = -EPERM;
		break;
	case PVM_HC_IRQ_WIN:
	case PVM_HC_IRQ_HALT:
		ret = v->registered ? 0 : -EPERM;
		v->halted = v->registered && R(16) == PVM_HC_IRQ_HALT;
		break;
	case PVM_HC_PV_SET_ENTRY: {
		unsigned long entry = R(10), gpa, flags;

		if (!v->registered || !v->pv_mode) { ret = -EPERM; break; }
		if ((entry & 1) || R(11) || R(12)) { ret = -EINVAL; break; }
		gpa = pvm_translate(v, entry, &flags);
		if (!gpa || (flags & _PAGE_USER) || !(flags & _PAGE_EXEC) ||
		    !pvm_gpa(v, gpa, 2)) { ret = -EFAULT; break; }
		v->pv_entry_gva = entry;
		v->pv_context_valid = false;
		ret = 0;
		break;
	}
	case PVM_HC_PV_RETURN:
		ret = pv_return_user(v, R(10));
		if (!ret)
			return 1; /* user GPRs/PC are restored; do not use generic tail */
		v->n_pv_reject++;
		break;
	case PVM_HC_PV_IRQ_SAVE:
		pv_irq_trace(v);
		if (!v->registered || !v->pv_mode) { ret = -EPERM; break; }
		ret = v->csr[0] & SR_SIE; /* read-only */
		break;
	case PVM_HC_PV_IRQ_DISABLE:
		pv_irq_trace(v);
		if (!v->registered || !v->pv_mode) { ret = -EPERM; break; }
		ret = v->csr[0] & SR_SIE; /* old value before atomic clear */
		v->csr[0] &= ~SR_SIE;
		break;
	case PVM_HC_PV_IRQ_RESTORE:
		pv_irq_trace(v);
		if (!v->registered || !v->pv_mode) { ret = -EPERM; break; }
		v->csr[0] = (v->csr[0] & ~SR_SIE) | (R(10) & SR_SIE);
		ret = 0;
		break;
	case PVM_HC_STATS_RESET: {
		/* Start a measurement window: record the current counter
		 * values as the window BASE.  Deltas are returned by
		 * STATS_READ.  v->exits is windowed too (not zeroed);
		 * the first-entry SPP assertion is unaffected. */
		if (!v->registered) {
			ret = -EPERM;
			break;
		}
		v->w_user_ecall = v->n_user_ecall;
		v->w_sbi = v->n_sbi;
		v->w_hypercall = v->n_hypercall;
		v->w_csr = v->n_csr;
		v->w_sret = v->n_sret;
		v->w_sfence = v->n_sfence;
		v->w_satp_write = v->n_satp_write;
		v->w_fill = v->n_fill;
		v->w_inj_fault = v->n_inj_fault;
		v->w_inj_irq = v->n_inj_irq;
		v->w_host_irq = v->n_host_irq;
		v->w_vmm_exit = v->n_vmm_exit;
		v->w_wfi = v->n_wfi;
		v->w_inj_ill = v->n_inj_ill;
		v->w_exits = v->exits;
		v->w_pv_entry = v->n_pv_entry;
		v->w_pv_return = v->n_pv_return;
		v->w_pv_reject = v->n_pv_reject;
		v->win_active = 1;
		ret = 0;
		break;
	}
	case PVM_HC_STATS_READ: {
		/* Write per-counter deltas to a DEDICATED stats buffer
		 * whose GPA the guest passes in a1 (page-aligned, 4KiB,
		 * must not overlap the PVCS page -- PVCS carries the
		 * event ABI reserved for stage E).  Counters stay
		 * host-authoritative; the copy is a guest-writable
		 * snapshot, not a trust boundary. */
		u64 *dst;
		u64 gpa = R(10); /* single-arg convention: a0 carries the GPA */
		if (!v->registered || !v->win_active) {
			ret = -EPERM;
			break;
		}
		if ((gpa & 0xfff) || gpa == v->pvcs ||
		    !pvm_gpa(v, gpa, 4096)) {
			ret = -EINVAL;
			break;
		}
		dst = (u64 *)pvm_gpa(v, gpa, 4096);
		/* Schema v2: 16 u64 slots; ALL values are deltas within
		 * [RESET, READ).  Slot 14 = schema version (2); slot 15
		 * = illegal injections in-window. */
		dst[0] = v->n_user_ecall - v->w_user_ecall;
		dst[1] = v->n_sbi - v->w_sbi;
		dst[2] = v->n_hypercall - v->w_hypercall;
		dst[3] = v->n_csr - v->w_csr;
		dst[4] = v->n_sret - v->w_sret;
		dst[5] = v->n_sfence - v->w_sfence;
		dst[6] = v->n_satp_write - v->w_satp_write;
		dst[7] = v->n_fill - v->w_fill;
		dst[8] = v->n_inj_fault - v->w_inj_fault;
		dst[9] = v->n_inj_irq - v->w_inj_irq;
		dst[10] = v->n_host_irq - v->w_host_irq;
		dst[11] = v->n_vmm_exit - v->w_vmm_exit;
		dst[12] = v->n_wfi - v->w_wfi;
		dst[13] = v->exits - v->w_exits;
		dst[14] = 2; /* schema version */
		dst[15] = v->n_inj_ill - v->w_inj_ill;
		dst[16] = v->n_pv_entry - v->w_pv_entry;
		dst[17] = v->n_pv_return - v->w_pv_return;
		dst[18] = v->n_pv_reject - v->w_pv_reject;
		v->win_active = 0;
		ret = 0;
		break;
	}
	}
	R(10) = ret;
	R(11) = 0;
	R(32) += 4;
	return 1;
}
static int sbi(struct riscv_pvm_vcpu *v)
{
	int __rc;
	unsigned long eid = R(17), fid = R(16), err = 0, val = 0;
	struct kvm_run *run = v->vcpu->run;
	if (eid == PVM_HC_EID) {
		v->n_hypercall++;
		__rc = hypercall(v);
		return __rc;
	}
	v->n_sbi++;
	if (eid == 0x10) {
		switch (fid) {
		case 0:
			val = 0x02000000;
			break;
		case 1:
			val = 0x50564d;
			break;
		case 2:
			val = 2;
			break;
		case 3:
			val = R(10) == 0x10 || R(10) == 0x54494d45 ||
			      R(10) == 0x53525354 || R(10) == 0x4442434e;
			break;
		default:
			val = 0;
			break;
		}
	} else if (eid == 0x54494d45 && fid == 0) {
		v->deadline = R(10);
		v->csr[0x44] &= ~0x20UL;
	} else if (eid == 0x53525354 && fid == 0) {
		pr_info("PVM_KVM_STATS exits=%llu faults=%llu user_entries=%llu timer_irqs=%llu schedules=%llu switches=%llu registered=%u\n",
			v->exits, v->faults, v->user_entries, v->timer_irqs,
			v->schedules, v->switches, v->registered);
		run->exit_reason = KVM_EXIT_SYSTEM_EVENT;
		run->system_event.type = R(10) == 0 ?
						 KVM_SYSTEM_EVENT_SHUTDOWN :
						 KVM_SYSTEM_EVENT_RESET;
		run->system_event.ndata = 1;
		run->system_event.data[0] = R(11);
		return 0;
	} else if ((eid == 0x4442434e && (fid == 0 || fid == 2)) || eid == 1) {
		unsigned int i;
		run->exit_reason = KVM_EXIT_RISCV_SBI;
		run->riscv_sbi.extension_id = eid;
		run->riscv_sbi.function_id = fid;
		for (i = 0; i < 6; i++)
			run->riscv_sbi.args[i] = R(10 + i);
		v->sbi_pending = true;
		return 0;
	} else if (eid == 0x4442434e && fid == 1) {
		val = 0;
	} else if (eid == 2) {
		R(10) = -1UL;
		R(32) += 4;
		return 1;
	} else
		err = -2UL;
	R(10) = err;
	R(11) = val;
	R(32) += 4;
	return 1;
}
int pvm_emulate(struct riscv_pvm_vcpu *v)
{
	unsigned long c = R(34), pc = R(32), pa, pte = 0, ins, csr, op, rs, rd,
		      old, x, nv;
	unsigned short *p;
	int ret;
	if (c >> 63) {
		v->n_host_irq++;
		if (pv_debug && v->pv_mode && v->pv_irq_diag++ < 4)
			pr_info("PVM_PV_HOSTIRQ pc=%lx virtual_s=%d sie=%lx\n",
				R(32), v->supervisor, v->csr[0] & SR_SIE);
		return 1; /* Physical host IRQ: normal Linux handles it after exit. */
	}
	if (c == 12 || c == 13 || c == 15) {
		/* Fill the page the FAULT names (stval): for a 4-byte instruction
   * crossing a page boundary tval is the NEXT page, not the PC page.
   * The PC itself is not advanced; the guest retries in place. */
		ret = pvm_page_fault(v, c, R(35));
		if (ret < 0)
			return ret;
		if (ret == 0) {
			/* fill declined: this fault IS delivered */
			pvm_inject(v, c, R(35));
		}
		/* ret==1: shadow filled in place; retry, no injection */
		return 1;
	}
	if (c == 8) {
		if (v->supervisor)
			return sbi(v);
		if (pv_enter_syscall(v))
			v->n_user_ecall++;
		else
			pvm_inject(v, 8, 0);
		return 1;
	}
	if (c != 2) {
		pvm_inject(v, c, R(35));
		return 1;
	}
	pa = pvm_translate(v, pc, &pte);
	p = pvm_gpa(v, pa, 2);
	if (!p)
		return -EFAULT;
	ins = *p;
	if ((ins & 3) == 3) {
		pa = pvm_translate(v, pc + 2, &pte);
		p = pvm_gpa(v, pa, 2);
		if (!p)
			return -EFAULT;
		ins |= (unsigned long)*p << 16;
	}
	csr = ins >> 20;
	op = ins >> 12 & 7;
	rs = ins >> 15 & 31;
	rd = ins >> 7 & 31;
	if (!v->supervisor) {
		if ((ins & 0x7f) == 0x73 &&
		    (op == 2 || op == 3 || op == 6 || op == 7) && !rs &&
		    csr >= 0xc00 && csr <= 0xc02 &&
		    (v->csr[6] & (1UL << (csr - 0xc00)))) {
			if (readcsr(v, csr, &x))
				return -EINVAL;
			if (rd)
				R(rd) = x;
			R(32) += 4;
			return 1;
		}
		pvm_inject(v, 2, ins);
		return 1;
	}
	if (ins == 0x10200073) {
		unsigned long st = v->csr[0];
		v->n_sret++;
		v->supervisor = !!(st & SR_SPP);
		v->csr[0] = (st & ~(SR_SIE | SR_SPP)) |
			    ((st & SR_SPIE) ? SR_SIE : 0) | SR_SPIE;
		if (!v->supervisor)
			v->user_entries++;
		R(32) = v->csr[0x41];
		return 1;
	}
	if ((ins & 0xfe007fffUL) == 0x12000073UL) {
		v->n_sfence++;
		pvm_reset_roots(v);
		R(32) += 4;
		return 1;
	}
	if (ins == 0x10500073) {
		v->n_wfi++;
		v->halted = true;
		R(32) += 4;
		return 1;
	}
	if ((ins & 0x7f) != 0x73 || !op || op == 4) {
		/* Virtual-kernel unknown instruction: inject a virtual
		 * illegal-instruction exception so the guest kernel's own
		 * handler decides; only host-internal errors (translation
		 * of the pc, backing access) fail-stop above. */
		pvm_inject(v, 2, ins);
		return 1;
	}
	if (readcsr(v, csr, &old)) {
		/* Unimplemented/supervisor-only CSR for this guest: also a
		 * virtual illegal, not a host failure. */
		pvm_inject(v, 2, ins);
		return 1;
	}
	v->n_csr++; /* confirmed CSR instruction being emulated */
	x = op & 4 ? rs : (rs ? R(rs) : 0);
	nv = old;
	switch (op & 3) {
	case 1:
		nv = x;
		break;
	case 2:
		nv |= x;
		break;
	case 3:
		nv &= ~x;
		break;
	}
	if ((op & 3) == 1 || rs) {
		ret = writecsr(v, csr, nv);
		if (ret) {
			pvm_inject(v, 2, ins);
			return 1;
		}
	}
	if (rd)
		R(rd) = old;
	R(32) += 4;
	return 1;
}
