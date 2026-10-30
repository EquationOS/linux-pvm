// SPDX-License-Identifier: GPL-2.0
#include "pvm.h"
#include <linux/module.h>
#include <linux/anon_inodes.h>
#include <linux/kvm_dirty_ring.h>
#include <linux/kvm_irqfd.h>
#include <linux/entry-kvm.h>
#include <linux/sched/clock.h>
#include <linux/uaccess.h>
#include <asm/cpufeature.h>

static bool pv_fast;
module_param(pv_fast, bool, 0444);
MODULE_PARM_DESC(pv_fast, "Enable PV fast gate (0=slow, 1=fast, default 0)");

const struct _kvm_stats_desc kvm_vm_stats_desc[] = { KVM_GENERIC_VM_STATS() };
const struct _kvm_stats_desc kvm_vcpu_stats_desc[] = {
	KVM_GENERIC_VCPU_STATS()
};
#define HEADER(name)                                                           \
	const struct kvm_stats_header kvm_##name##_stats_header = {            \
		.name_size = KVM_STATS_NAME_SIZE,                              \
		.num_desc = ARRAY_SIZE(kvm_##name##_stats_desc),               \
		.id_offset = sizeof(struct kvm_stats_header),                  \
		.desc_offset =                                                 \
			sizeof(struct kvm_stats_header) + KVM_STATS_NAME_SIZE, \
		.data_offset = sizeof(struct kvm_stats_header) +               \
			       KVM_STATS_NAME_SIZE +                           \
			       sizeof(kvm_##name##_stats_desc)                 \
	}
HEADER(vm);
HEADER(vcpu);
/* Generic irqchip/MSI hooks are unused: the bounded backend handles
 * virtual TIME through timer() and the guest console through SBI exits.
 */
int kvm_cpu_has_pending_timer(struct kvm_vcpu *vcpu)
{
	return 0;
}
int kvm_set_msi(struct kvm_kernel_irq_routing_entry *e, struct kvm *kvm,
		int irq_source_id, int level, bool line_status)
{
	return -ENOSYS;
}

int kvm_arch_enable_virtualization_cpu(void)
{
	return 0;
}
void kvm_arch_disable_virtualization_cpu(void)
{
}
long kvm_arch_dev_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	return -EINVAL;
}
int kvm_arch_init_vm(struct kvm *kvm, unsigned long type)
{
	if (type)
		return -EINVAL;
	kvm->arch.pvm = kzalloc(sizeof(*kvm->arch.pvm), GFP_KERNEL);
	return kvm->arch.pvm ? 0 : -ENOMEM;
}
void kvm_arch_destroy_vm(struct kvm *kvm)
{
	struct riscv_pvm_vm *p = kvm->arch.pvm;
	unsigned long i;
	kvm_destroy_vcpus(kvm);
	if (!p)
		return;
	for (i = 0; i < p->nr_pages; i++)
		if (p->pages && p->pages[i]) {
			set_page_dirty_lock(p->pages[i]);
			put_page(p->pages[i]);
		}
	kvfree(p->pages);
	kfree(p);
	kvm->arch.pvm = NULL;
}
int kvm_arch_vcpu_precreate(struct kvm *kvm, unsigned int id)
{
	return id || atomic_read(&kvm->online_vcpus) ? -EINVAL : 0;
}
int kvm_arch_vcpu_create(struct kvm_vcpu *vcpu)
{
	struct riscv_pvm_vcpu *v;
	int err;
	v = kzalloc(sizeof(*v), GFP_KERNEL);
	if (!v)
		return -ENOMEM;
	v->vcpu = vcpu;
	v->supervisor = true; v->pv_fast = pv_fast;
	v->deadline = ~0UL;
	v->csr[6] = 7;
	err = pvm_mmu_init(v);
	if (err) {
		pvm_mmu_destroy(v);
		kfree(v);
		return err;
	}
	BUILD_BUG_ON(offsetof(struct pvm_switch_state, hfp) != PVM_HFP);
	BUILD_BUG_ON(sizeof(struct pvm_switch_state) > PAGE_SIZE);
	v->s->g[32] = PVM_RAM_BASE;
	v->s->g[2] = 0x80600000;
	v->s->g[11] = 0x87e00000;
	vcpu->arch.pvm = v;
	pr_info("PVM_KVM_VCPU created: S-mode host, physical U-mode guest, no H extension required\n");
	return 0;
}
void kvm_arch_vcpu_postcreate(struct kvm_vcpu *v)
{
}
void kvm_arch_vcpu_destroy(struct kvm_vcpu *vcpu)
{
	struct riscv_pvm_vcpu *v = vcpu->arch.pvm;
	if (v) {
		if (v->pv_mode)
			pr_info("PVM_GATE_FINAL fast=%u in=%lu out=%lu irq=%lu physical=%lu c_exits=%llu host_irq=%llu\n",
				v->pv_fast, v->s->fast_in, v->s->fast_out,
				v->s->fast_irq, v->s->fast_phys_traps,
				v->exits, v->n_host_irq);
		pvm_mmu_destroy(v);
		kfree(v);
	}
	vcpu->arch.pvm = NULL;
}
void kvm_arch_vcpu_load(struct kvm_vcpu *v, int cpu)
{
}
void kvm_arch_vcpu_put(struct kvm_vcpu *v)
{
}
void kvm_arch_vcpu_blocking(struct kvm_vcpu *v)
{
}
void kvm_arch_vcpu_unblocking(struct kvm_vcpu *v)
{
}
int kvm_arch_vcpu_runnable(struct kvm_vcpu *v)
{
	return 1;
}
int kvm_arch_vcpu_should_kick(struct kvm_vcpu *v)
{
	return 1;
}
bool kvm_arch_vcpu_in_kernel(struct kvm_vcpu *v)
{
	return v->arch.pvm->supervisor;
}
vm_fault_t kvm_arch_vcpu_fault(struct kvm_vcpu *v, struct vm_fault *f)
{
	return VM_FAULT_SIGBUS;
}
static int one_reg(struct kvm_vcpu *vcpu, struct kvm_one_reg *reg, bool set)
{
	struct riscv_pvm_vcpu *v = vcpu->arch.pvm;
	unsigned long num = reg->id & 0xffff,
		      typ = reg->id & KVM_REG_RISCV_TYPE_MASK, val = 0,
		      *p = NULL;
	if ((reg->id & (KVM_REG_ARCH_MASK | KVM_REG_SIZE_MASK)) !=
	    (KVM_REG_RISCV | KVM_REG_SIZE_U64))
		return -EINVAL;
	if (typ == KVM_REG_RISCV_CORE) {
		if (num == 32) {
			if (set) {
				if (get_user(val,
					     (unsigned long __user *)reg->addr))
					return -EFAULT;
				if (val > 1)
					return -EINVAL;
				v->supervisor = val;
				return 0;
			}
			return put_user(v->supervisor,
					(unsigned long __user *)reg->addr);
		}
		if (num < 32)
			p = &v->s->g[num ? num : 32];
	}
	if (!p)
		return -ENOENT;
	if (set) {
		if (get_user(val, (unsigned long __user *)reg->addr))
			return -EFAULT;
		*p = val;
	} else if (put_user(*p, (unsigned long __user *)reg->addr))
		return -EFAULT;
	return 0;
}
long kvm_arch_vcpu_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct kvm_vcpu *vcpu = f->private_data;
	struct kvm_one_reg reg;
	if (cmd == KVM_GET_ONE_REG || cmd == KVM_SET_ONE_REG) {
		if (copy_from_user(&reg, (void __user *)arg, sizeof(reg)))
			return -EFAULT;
		return one_reg(vcpu, &reg, cmd == KVM_SET_ONE_REG);
	}
	return -EINVAL;
}
long kvm_arch_vcpu_async_ioctl(struct file *f, unsigned int cmd,
			       unsigned long arg)
{
	return -ENOIOCTLCMD;
}
int kvm_arch_vcpu_ioctl_get_regs(struct kvm_vcpu *v, struct kvm_regs *r)
{
	return -EINVAL;
}
int kvm_arch_vcpu_ioctl_set_regs(struct kvm_vcpu *v, struct kvm_regs *r)
{
	return -EINVAL;
}
int kvm_arch_vcpu_ioctl_get_sregs(struct kvm_vcpu *v, struct kvm_sregs *r)
{
	return -EINVAL;
}
int kvm_arch_vcpu_ioctl_set_sregs(struct kvm_vcpu *v, struct kvm_sregs *r)
{
	return -EINVAL;
}
int kvm_arch_vcpu_ioctl_get_fpu(struct kvm_vcpu *v, struct kvm_fpu *r)
{
	return -EINVAL;
}
int kvm_arch_vcpu_ioctl_set_fpu(struct kvm_vcpu *v, struct kvm_fpu *r)
{
	return -EINVAL;
}
int kvm_arch_vcpu_ioctl_translate(struct kvm_vcpu *v, struct kvm_translation *t)
{
	return -EINVAL;
}
int kvm_arch_vcpu_ioctl_set_guest_debug(struct kvm_vcpu *v,
					struct kvm_guest_debug *d)
{
	return -EINVAL;
}
int kvm_arch_vcpu_ioctl_get_mpstate(struct kvm_vcpu *v, struct kvm_mp_state *s)
{
	s->mp_state = KVM_MP_STATE_RUNNABLE;
	return 0;
}
int kvm_arch_vcpu_ioctl_set_mpstate(struct kvm_vcpu *v, struct kvm_mp_state *s)
{
	return s->mp_state == KVM_MP_STATE_RUNNABLE ? 0 : -EINVAL;
}
int kvm_vm_ioctl_check_extension(struct kvm *kvm, long ext)
{
	switch (ext) {
	case KVM_CAP_USER_MEMORY:
	case KVM_CAP_ONE_REG:
	case KVM_CAP_IMMEDIATE_EXIT:
	case KVM_CAP_MP_STATE:
		return 1;
	/* KVM_CAP_DESTROY_MEMORY_REGION_WORKS is deliberately NOT
	 * advertised: the backend only supports KVM_MR_CREATE (see
	 * kvm_arch_prepare_memory_region rejecting everything else). */
	case KVM_CAP_MAX_VCPUS:
	case KVM_CAP_NR_VCPUS:
	case KVM_CAP_NR_MEMSLOTS:
		return 1;
	default:
		return 0;
	}
}
int kvm_arch_vm_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	return -EINVAL;
}
int kvm_vm_ioctl_irq_line(struct kvm *k, struct kvm_irq_level *l, bool s)
{
	return -ENXIO;
}
bool kvm_arch_irqchip_in_kernel(struct kvm *k)
{
	return false;
}
bool kvm_arch_can_set_irq_routing(struct kvm *k)
{
	return false;
}
int kvm_set_routing_entry(struct kvm *k, struct kvm_kernel_irq_routing_entry *e,
			  const struct kvm_irq_routing_entry *u)
{
	return -EINVAL;
}
int kvm_arch_set_irq_inatomic(struct kvm_kernel_irq_routing_entry *e,
			      struct kvm *k, int src, int level, bool status)
{
	return -EINVAL;
}
void kvm_arch_free_memslot(struct kvm *k, struct kvm_memory_slot *s)
{
}
void kvm_arch_memslots_updated(struct kvm *k, u64 gen)
{
}
void kvm_arch_commit_memory_region(struct kvm *k, struct kvm_memory_slot *o,
				   const struct kvm_memory_slot *n,
				   enum kvm_mr_change c)
{
}
int kvm_arch_prepare_memory_region(struct kvm *k,
				   const struct kvm_memory_slot *o,
				   struct kvm_memory_slot *n,
				   enum kvm_mr_change c)
{
	if (k->arch.pvm->bound)
		return -EBUSY;
	if (c != KVM_MR_CREATE)
		return -EINVAL;
	return n->id || n->flags || n->base_gfn != (PVM_RAM_BASE >> 12) ||
			       n->npages !=
				       ((PVM_RAM_END - PVM_RAM_BASE) >> 12) ?
		       -EINVAL :
		       0;
}
static void invalidate(struct kvm *k)
{
	if (k->arch.pvm && k->arch.pvm->bound) {
		atomic_set(&k->arch.pvm->invalid, 1);
		kvm_make_all_cpus_request(k, KVM_REQ_OUTSIDE_GUEST_MODE);
	}
}
void kvm_arch_flush_shadow_all(struct kvm *k)
{
	invalidate(k);
}
void kvm_arch_flush_shadow_memslot(struct kvm *k, struct kvm_memory_slot *s)
{
	invalidate(k);
}
bool kvm_unmap_gfn_range(struct kvm *k, struct kvm_gfn_range *r)
{
	invalidate(k);
	return false;
}
bool kvm_age_gfn(struct kvm *k, struct kvm_gfn_range *r)
{
	return false;
}
bool kvm_test_age_gfn(struct kvm *k, struct kvm_gfn_range *r)
{
	return false;
}
void kvm_arch_sync_dirty_log(struct kvm *k, struct kvm_memory_slot *s)
{
}
void kvm_arch_mmu_enable_log_dirty_pt_masked(struct kvm *k,
					     struct kvm_memory_slot *s, gfn_t o,
					     unsigned long m)
{
}
static void timer(struct riscv_pvm_vcpu *v)
{
	if (v->deadline != ~0UL && (s64)(get_cycles64() - v->deadline) >= 0)
		v->csr[0x44] |= 0x20;
	/* WFI semantics: wake the hart when the local timer interrupt is
  * enabled AND pending, even if global SIE masks delivery -- the
  * guest then re-evaluates (and typically clears STIP via a new
  * set_timer or takes the IRQ once it enables SIE).  Only inject the
  * virtual interrupt when the virtual global state allows it. */
	if (v->halted && (v->csr[4] & v->csr[0x44] & 0x20))
		v->halted = false;
	if ((v->csr[0x44] & v->csr[4] & 0x20) &&
	    (!v->supervisor || (v->csr[0] & SR_SIE))) {
		v->timer_irqs++;
		pvm_inject(v, 1UL << 63 | 5, 0);
		v->halted = false;
	}
}
int kvm_arch_vcpu_ioctl_run(struct kvm_vcpu *vcpu)
{
	struct riscv_pvm_vcpu *v = vcpu->arch.pvm;
	int ret = 1;
	if (!vcpu->wants_to_run)
		return -EINTR;
	mutex_lock(&vcpu->kvm->slots_lock);
	kvm_vcpu_srcu_read_lock(vcpu);
	ret = pvm_bind_memory(vcpu);
	mutex_unlock(&vcpu->kvm->slots_lock);
	if (ret) {
		kvm_vcpu_srcu_read_unlock(vcpu);
		return ret;
	}
	if (v->sbi_pending) {
		v->s->g[10] = vcpu->run->riscv_sbi.ret[0];
		v->s->g[11] = vcpu->run->riscv_sbi.ret[1];
		v->s->g[32] += 4;
		v->sbi_pending = false;
	}
	vcpu_load(vcpu);
	kvm_sigset_activate(vcpu);
	ret = 1;
	while (ret > 0) {
		if (signal_pending(current) ||
		    READ_ONCE(vcpu->run->immediate_exit__unsafe)) {
			ret = -EINTR;
			break;
		}
		if (atomic_read(&vcpu->kvm->arch.pvm->invalid)) {
			ret = -EFAULT;
			break;
		}
		cond_resched();
		timer(v);
		if (v->halted) {
			ktime_t delay = ns_to_ktime(
				1000000); /* bounded wait, host remains schedulable */
			kvm_vcpu_srcu_read_unlock(vcpu);
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_hrtimeout(&delay, HRTIMER_MODE_REL);
			__set_current_state(TASK_RUNNING);
			v->schedules++;
			kvm_vcpu_srcu_read_lock(vcpu);
			continue;
		}
		preempt_disable();
		local_irq_disable();
		vcpu->mode = IN_GUEST_MODE;
		kvm_vcpu_srcu_read_unlock(vcpu);
		smp_mb__after_srcu_read_unlock();
		if (atomic_read(&vcpu->kvm->arch.pvm->invalid) ||
		    signal_pending(current)) {
			vcpu->mode = OUTSIDE_GUEST_MODE;
			local_irq_enable();
			preempt_enable();
			kvm_vcpu_srcu_read_lock(vcpu);
			ret = -EINTR;
			break;
		}
		v->s->guest_satp = pvm_real_satp(v);
		v->s->g[33] = (2UL << 32) | SR_SPIE |
			      (v->csr[0] & (SR_FS | PVM_MXR));
		/* E2: sync virtual state C->asm before every switch */
		v->s->fast_virt_sup = v->supervisor ? 1 : 0;
		v->s->fast_virt_status = v->csr[0];
		v->s->fast_virt_sepc = v->csr[0x41];
		v->s->fast_virt_sscratch = v->csr[0x40]; /* sscratch = csr[0x140-0x100] */
		v->s->fast_virt_scause = v->csr[0x42];
		v->s->fast_virt_stval = v->csr[0x43];
		/* E2: refresh the fast cache (full rewalk; conservative) */
		v->s->fast_cache_valid = 0;
		if (v->pv_fast)
			(void)pvm_prepare_fast_cache(v);
		guest_timing_enter_irqoff();
		riscv_pvm_switch(v->s,
				 (8UL << 60) |
					 (virt_to_phys(v->bridge.root) >> 12));
		vcpu->mode = OUTSIDE_GUEST_MODE;
		v->exits++;
		v->switches++;
		/* E2: full asm->C sync after every switch.  The fast gate
		 * may have changed identity/CSR/counters without C; we
		 * read back the entire virtual state and fold the asm
		 * counter DELTAS into the cumulative categorical counters
		 * (each fast syscall counts as user_ecall, each fast
		 * PV_RETURN/IRQ helper as hypercall, each fast user return
		 * as user_entries -- exactly once, never double-counted
		 * because the C emulator path never sees these traps). */
		{
			u64 d_fast_in, d_fast_out, d_fast_irq;

			v->supervisor = v->s->fast_virt_sup != 0;
			v->csr[0] = v->s->fast_virt_status;
			v->csr[0x41] = v->s->fast_virt_sepc;
			v->csr[0x40] = v->s->fast_virt_sscratch;
			v->csr[0x42] = v->s->fast_virt_scause;
			v->csr[0x43] = v->s->fast_virt_stval;

			/* fold asm deltas into categorical totals */
			d_fast_in = v->s->fast_in - v->n_fast_in;
			d_fast_out = v->s->fast_out - v->n_fast_out;
			d_fast_irq = v->s->fast_irq - v->n_fast_irq;

			v->n_user_ecall += d_fast_in;
			v->n_pv_entry += d_fast_in;
			v->n_pv_return += d_fast_out;
			v->user_entries += d_fast_out;
			v->n_hypercall += d_fast_irq + d_fast_out;

			/* update mirrors for the next delta */
			v->n_fast_in = v->s->fast_in;
			v->n_fast_out = v->s->fast_out;
			v->n_fast_irq = v->s->fast_irq;
			v->n_fast_fallback = v->s->fast_fallback;
			v->n_phys_traps = v->s->fast_phys_traps;
			v->n_root_writes = v->s->fast_root_writes;
			v->n_sfences_asm = v->s->fast_sfences;
		}
		local_irq_enable();
		local_irq_disable();
		guest_timing_exit_irqoff();
		local_irq_enable();
		preempt_enable();
		kvm_vcpu_srcu_read_lock(vcpu);
		/* Stage evidence: the gate returns the host status; SPP must never
   * be set -- the guest physically ran in U-mode the whole time. */
		if (v->exits == 1)
			pr_info("PVM_KVM_SWITCH_OK: first guest entry returned, saved sstatus.SPP=%lu switches=%llu\n",
				(v->s->g[33] & SR_SPP) ? 1 : 0UL, v->switches);
		if (v->s->g[33] & SR_SPP) {
			ret = -EPERM;
			break;
		}
		if ((v->s->g[33] & SR_FS) == SR_FS_DIRTY && (v->csr[0] & SR_FS))
			v->csr[0] |= SR_FS_DIRTY;
		ret = pvm_emulate(v);
		if (ret < 0) {
			pr_err("PVM_KVM emulation failed %d pc=%lx cause=%lx tval=%lx virtual_s=%d\n",
			       ret, v->s->g[32], v->s->g[34], v->s->g[35],
			       v->supervisor);
		}
	}
	kvm_sigset_deactivate(vcpu);
	if (ret >= 0)
		v->n_vmm_exit++; /* successful userspace-visible exit (not
				  * -EINTR/-EFAULT ioctl errors) */
	vcpu_put(vcpu);
	kvm_vcpu_srcu_read_unlock(vcpu);
	return ret;
}
static int __init pvm_init(void)
{
	pr_info("KVM: RISC-V PVM S-mode backend selected (physical U-mode guests)\n");
	return kvm_init(sizeof(struct kvm_vcpu), 0, THIS_MODULE);
}
module_init(pvm_init);
static void __exit pvm_exit(void)
{
	kvm_exit();
}
module_exit(pvm_exit);
MODULE_LICENSE("GPL");
