// SPDX-License-Identifier: GPL-2.0
#include "pvm.h"
#define R(i) v->s->g[i]
void pvm_inject(struct riscv_pvm_vcpu *v, unsigned long cause,
		unsigned long val)
{
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
	v->csr[c - 0x100] = val;
	if (c == CSR_SSTATUS && ((old ^ val) & (SR_SUM | PVM_MXR)))
		pvm_reset_roots(v);
	return 0;
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
		ret = PVM_FEATURE_CSR_TRAPS;
		break;
	case PVM_HC_REGISTER_PVCS:
		if (v->registered)
			ret = -EBUSY;
		else if (R(10) != PVM_REGISTER_CSR_EVENTS || (R(11) & 4095) ||
			 !pvm_gpa(v, R(11), 4096) || R(12) != R(4) ||
			 !pvm_translate(v, R(12), &pte) || (pte & 0x10))
			ret = -EINVAL;
		else {
			v->pvcs = R(11);
			v->kernel_tp = R(12);
			v->registered = true;
			ret = 0;
		}
		break;
	case PVM_HC_DEREGISTER:
		v->registered = false;
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
	}
	R(10) = ret;
	R(11) = 0;
	R(32) += 4;
	return 1;
}
static int sbi(struct riscv_pvm_vcpu *v)
{
	unsigned long eid = R(17), fid = R(16), err = 0, val = 0;
	struct kvm_run *run = v->vcpu->run;
	if (eid == PVM_HC_EID)
		return hypercall(v);
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
	if (c >> 63)
		return 1; /* Physical host IRQ: normal Linux handles it after exit. */
	if (c == 12 || c == 13 || c == 15) {
		/* Fill the page the FAULT names (stval): for a 4-byte instruction
   * crossing a page boundary tval is the NEXT page, not the PC page.
   * The PC itself is not advanced; the guest retries in place. */
		ret = pvm_page_fault(v, c, R(35));
		if (ret)
			return ret;
		pvm_inject(v, c, R(35));
		return 1;
	}
	if (c == 8) {
		if (v->supervisor)
			return sbi(v);
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
		v->supervisor = !!(st & SR_SPP);
		v->csr[0] = (st & ~(SR_SIE | SR_SPP)) |
			    ((st & SR_SPIE) ? SR_SIE : 0) | SR_SPIE;
		if (!v->supervisor)
			v->user_entries++;
		R(32) = v->csr[0x41];
		return 1;
	}
	if ((ins & 0xfe007fffUL) == 0x12000073UL) {
		pvm_reset_roots(v);
		R(32) += 4;
		return 1;
	}
	if (ins == 0x10500073) {
		v->halted = true;
		R(32) += 4;
		return 1;
	}
	if ((ins & 0x7f) != 0x73 || !op || op == 4)
		return -EOPNOTSUPP;
	if (readcsr(v, csr, &old))
		return -EOPNOTSUPP;
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
		if (ret)
			return ret;
	}
	if (rd)
		R(rd) = old;
	R(32) += 4;
	return 1;
}
