// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PVM (Pagetable-based Virtual Machine) guest support for RISC-V.
 *
 * This is the RV64 counterpart of the x86_64 guest implementation in
 * arch/x86/kernel/pvm.c, see Documentation/arch/riscv/pvm.rst and
 * Documentation/virt/kvm/x86/pvm-spec.rst for the ABI.
 *
 * A PVM guest kernel runs in U-mode under the host kernel: privileged
 * instructions (satp writes, sfence.vma, wfi, ...) trap into the host and
 * are dispatched to the handlers here through hypercalls and event
 * delivery.
 *
 * Status: a Linux S-mode KVM PVM host supports a bounded CSR reference
 * and explicit PV syscall entry/return, with optional software S-gate
 * acceleration. Both guest kernel and user programs run physically in
 * U-mode without H. Ordinary boots without a PVM marker remain native.
 * The older synthetic event/RETU protocol is still unsupported.
 *
 * Copyright (C) 2020 Ant Group
 * RISC-V port for the USyscall project, 2026.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */
#define pr_fmt(fmt) "pvm-guest: " fmt

#include <linux/init.h>
#include <linux/mm.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/percpu.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>

#include <asm/current.h>
#include <asm/ptrace.h>
#include <linux/irqflags.h>
#include <linux/thread_info.h>

#include <asm/entry-common.h>
#include <asm/pvm_para.h>

bool pvm_guest_detected __ro_after_init;
static bool pvm_csr_guest __ro_after_init;

static bool pvm_pv_guest __ro_after_init;
static bool __initdata pvm_pv_mode_requested;

/*
 * E1: pv_mode=csr (default) keeps the CSR-trap bootstrap; pv_mode=pv
 * requests the full PV slow path (PV entry + PV_RETURN + irq/mm
 * hooks).  Both require a matching host; a negotiation failure is a
 * hard failure (strict), never a silent fallback to CSR claiming PV.
 */
static int __init pvm_pv_mode_cmdline(char *arg)
{
	if (!arg)
		return -EINVAL;
	if (!strcmp(arg, "pv"))
		pvm_pv_mode_requested = true;
	else if (strcmp(arg, "csr"))
		return -EINVAL;
	return 0;
}
early_param("pvm_mode", pvm_pv_mode_cmdline);

DEFINE_PER_CPU_PAGE_ALIGNED(struct pvm_vcpu_struct, pvm_vcpu_struct);

/*
 * On x86_64 the guest detects PVM with a synthetic instruction sequence
 * that only traps when the kernel itself runs in user mode (underlying
 * CS.RPL == 3 and IF == 1), which no other hypervisor produces.
 *
 * RV64 has no equivalent self-contained probe: privileged CSR reads from
 * an S-mode kernel succeed natively and would have to be made to fault to
 * be distinguishable, with no way to keep a native S-mode boot trap-free.
 * Detection is therefore based on the device tree handed over by the
 * loader/host instead: a PVM host for RV64 must expose a top-level node
 * with compatible "pvm,guest".  A native OpenSBI or bare-metal boot has no
 * such node, so the guest falls back to running natively.
 */
static bool __init pvm_detect(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "pvm,csr-guest");
	pvm_csr_guest = np != NULL;
	of_node_put(np);
	if (pvm_csr_guest) {
		/* pv_mode=pv upgrades a csr-guest DT marker to full PV */
		if (pvm_pv_mode_requested)
			pvm_pv_guest = true;
		return true;
	}
	np = of_find_compatible_node(NULL, NULL, "pvm,guest");
	of_node_put(np);

	return np != NULL;
}

/*
 * Legacy out-of-line PV wrappers. Live IRQ/MMU hooks use the minimal
 * pvm_hypercall.h helpers after PV negotiation; native boots never call
 * these wrappers. Fault and interrupt delivery still uses CSR emulation.
 */
void noinstr pvm_safe_halt(void)
{
	/* wfi traps from U-mode, halt through the host instead. */
	pvm_hypercall0(PVM_HC_IRQ_HALT);
}

void pvm_flush_tlb_all(void)
{
	/* sfence.vma with rs1=rs2=0 traps from U-mode. */
	pvm_hypercall0(PVM_HC_TLB_FLUSH);
}

void pvm_flush_tlb_current(void)
{
	pvm_hypercall0(PVM_HC_TLB_FLUSH_CURRENT);
}

void pvm_flush_tlb_page(unsigned long addr)
{
	pvm_hypercall1(PVM_HC_TLB_INVLPG, addr);
}

void pvm_load_pgtbl(unsigned long satp)
{
	/* Flags are reserved (zero): the host decodes MODE/ASID/PPN from
	 * the raw satp value and owns the flush policy. */
	pvm_hypercall2(PVM_HC_LOAD_PGTBL, 0, satp);
}

/*
 * Event dispatch skeleton.  Entered from pvm_event_entry once the host
 * injects an event (fault, interrupt or hypercall completion) with the
 * interrupted context saved in a host-provided frame.
 *
 * There are no functional handlers yet.  Resuming a synchronous event
 * unchanged would just refault forever, so every event is fatal for
 * now: log it and tell the entry to stop, which parks this vCPU in a
 * tight loop instead of corrupting the frame.
 *
 * Returns the frame to resume, or NULL when the event is fatal.
 */
__visible struct pt_regs *pvm_event_dispatch(unsigned long cause,
					     struct pt_regs *regs)
{
	pr_crit("PVM event cause 0x%lx at %px has no handler, stopping this vCPU\n",
		cause, (void *)regs->epc);
	return NULL;
}

/* Per-CPU memory may live in the vmalloc area; translate both ways. */
static unsigned long pvm_ptr_to_phys(void *p)
{
	if (is_vmalloc_addr(p))
		return page_to_phys(vmalloc_to_page(p));
	return __pa(p);
}

/*
 * pvm=strict: registration failure is fatal.  A real U-mode guest
 * cannot fall back to running natively, so the U-mode boot path will
 * always register strictly; the permissive fallback below only exists
 * so an S-mode kernel with a (test) device-tree marker degrades to the
 * verified native path instead of crashing.  It is a negative test
 * aid, not a runtime mode of PVM.
 */
static bool pvm_strict;

static int __init pvm_cmdline_param(char *arg)
{
	if (arg && !strcmp(arg, "strict"))
		pvm_strict = true;
	return 0;
}
early_param("pvm", pvm_cmdline_param);

/*
 * Called when registration fails after PVM_HC_REGISTER_PVCS has
 * succeeded: the host holds our PVCS pointer, so the rollback must be
 * confirmed before anything else may continue.
 */
static void __init pvm_rollback(void)
{
	long ret = pvm_hypercall0(PVM_HC_DEREGISTER);

	if (ret)
		panic("PVM: DEREGISTER failed (ret %ld), host still holds the PVCS pointer\n",
		      ret);
}

static void __init pvm_no_host(const char *what, long ret, bool registered)
{
	if (registered)
		pvm_rollback();

	if (pvm_strict || pvm_csr_guest)
		panic("PVM: %s failed (ret %ld); a PVM host is mandatory\n",
		      what, ret);

	pr_warn("%s refused (ret %ld), no PVM host: continuing natively\n",
		what, ret);
	pvm_guest_detected = false;
}

/* Registration follows CSR-emulated bootstrap of the physical U-mode
 * kernel. The selected mode negotiates its feature bit and PVCS first;
 * explicit PV syscalls additionally register pvm_pv_entry. The legacy
 * synthetic event protocol remains separate and unsupported by this host.
 */
/* ---- D2: measurement window via /proc/pvm_stats ---- */
static u64 *pvm_stats_page;
static bool pvm_stats_on;

static bool pvm_stats_valid;

static ssize_t pvm_stats_write(struct file *f, const char __user *ub,
			       size_t n, loff_t *off)
{
	char cmd[8];

	if (n < 4 || n > 7 || copy_from_user(cmd, ub, n))
		return -EINVAL;
	cmd[n] = 0;
	if (!strncmp(cmd, "reset", 5)) {
		long r = pvm_hypercall0(PVM_HC_STATS_RESET);
		if (r) return -EIO;
		pvm_stats_on = true;
		return n;
	}
	if (!strncmp(cmd, "read", 4)) {
		long r;
		if (!pvm_stats_on) return -EIO;
		r = pvm_hypercall1(PVM_HC_STATS_READ,
			pvm_ptr_to_phys(pvm_stats_page));
		if (r) return -EIO;
		pvm_stats_on = false;
		pvm_stats_valid = (pvm_stats_page[14] == 3);
		return n;
	}
	return -EINVAL;
}

static ssize_t pvm_stats_read(struct file *f, char __user *ub,
			      size_t n, loff_t *off)
{
	/* Dump the last snapshot as one JSON line (schema v3). */
	char buf[1024];
	int len;

	if (!pvm_stats_page || !pvm_stats_valid)
		return 0;
	len = scnprintf(buf, sizeof(buf),
		"{\"type\":\"pvm_stats\",\"schema\":%llu,"
		"\"user_ecall\":%llu,\"sbi\":%llu,\"hypercall\":%llu,"
		"\"csr\":%llu,\"sret\":%llu,\"sfence\":%llu,"
		"\"satp\":%llu,\"fill\":%llu,\"inj_fault\":%llu,"
		"\"inj_irq\":%llu,\"host_irq\":%llu,\"vmm_exit\":%llu,"
		"\"wfi\":%llu,\"exits\":%llu,\"inj_ill\":%llu,"
		"\"pv_entry\":%llu,\"pv_return\":%llu,\"pv_reject\":%llu,"
		"\"phys_traps\":%llu,\"root_writes\":%llu,\"sfences\":%llu,"
		"\"fast_in\":%llu,\"fast_out\":%llu,\"fast_irq\":%llu,\"fast_fallback\":%llu}\n",
		pvm_stats_page[14], pvm_stats_page[0], pvm_stats_page[1],
		pvm_stats_page[2], pvm_stats_page[3], pvm_stats_page[4],
		pvm_stats_page[5], pvm_stats_page[6], pvm_stats_page[7],
		pvm_stats_page[8], pvm_stats_page[9], pvm_stats_page[10],
		pvm_stats_page[11], pvm_stats_page[12], pvm_stats_page[13],
		pvm_stats_page[15],
		pvm_stats_page[16], pvm_stats_page[17], pvm_stats_page[18],
		pvm_stats_page[19], pvm_stats_page[20], pvm_stats_page[21],
		pvm_stats_page[22], pvm_stats_page[23], pvm_stats_page[24],
		pvm_stats_page[25]);
	return simple_read_from_buffer(ub, n, off, buf, len);
}

static const struct proc_ops pvm_stats_ops = {
	.proc_write = pvm_stats_write,
	.proc_read = pvm_stats_read,
};

/*
 * E1 guest PV entry: the host (C slow path) enters here in guest kernel
 * context with kernel TP/GP/SP installed and a0 = pt_regs of the
 * user-mode event.  We jump into the normal kernel exception flow.
 * Async faults/IRQs keep the CSR event path (sepc/scause in the pt_regs
 * constructed by the host).
 */
bool pvm_pv_mode_active __ro_after_init;
EXPORT_SYMBOL_GPL(pvm_pv_mode_active);

asmlinkage void do_trap_ecall_u(struct pt_regs *regs);

/* Called from entry.S when PV_RETURN is rejected for a kernel-internal
 * frame.  This is a host-side bug; park with a message. */
asmlinkage void __noreturn pvm_pv_return_rejected(void)
{
	pr_emerg("pvm-guest: PV_RETURN rejected for kernel frame (host bug), parking\n");
	while (1)
		cpu_relax();
}
asmlinkage void pvm_pv_return(struct pt_regs *regs);

asmlinkage void __noreturn pvm_pv_entry(struct pt_regs *regs)
{
	/* The host enters here with kernel TP/GP/SP installed and
	 * a0 = pt_regs of a user-mode ecall (virtual identity USER at
	 * the time of the trap).  The frame lives at the registered
	 * kernel stack slot; virtual sscratch contract: PV kernel sees
	 * sscratch=0 (host owns it), USER would see saved kernel TP --
	 * matching native entry.S's TP/stack convention because the
	 * host installs kernel TP before entering. */
	do_trap_ecall_u(regs);
	/* After the syscall, perform the PV return: pass the (possibly
	 * updated) frame to the host.  The host validates and does the
	 * virtual sret.  If the host rejects (shouldn't for our own
	 * frame), we must NOT return to an undefined ra -- park. */
	pvm_pv_return(regs);
	/* host rejected: this is a host-side bug for a kernel-internal
	 * frame; park here rather than returning to garbage */
	pr_err("pvm-guest: PV_RETURN rejected for kernel frame\n");
	while (1)
		cpu_relax();
}

/*
 * PV_RETURN wrapper: called from ret_from_exception's PV branch with
 * the pt_regs pointer.  a0 = frame; returns only if the host rejected
 * it (should not happen for kernel-internal calls).
 */
asmlinkage void pvm_pv_return(struct pt_regs *regs)
{
	unsigned long __flags;

	/*
	 * Reset the task's saved kernel SP to just above this frame --
	 * mirroring what native ret_from_exception does (addi s0,sp,PT_SIZE;
	 * REG_S s0,TASK_TI_KERNEL_SP(tp)).  Without this, nested kernel
	 * traps during the syscall leave a mid-stack value in TI_KERNEL_SP;
	 * the next user entry (or copy_thread's canonical frame) then
	 * derives from a stale pointer and the frame address drifts down
	 * the stack on every kernel-nested syscall.  thread_info is at
	 * offset 0 of task_struct; tp holds current in the kernel.
	 */
	__flags = arch_local_irq_save();
	current_thread_info()->kernel_sp = (unsigned long)(regs + 1);

	/*
	 * Clear any dangling load reservation before returning to user
	 * context, mirroring the sc.d on PT_EPC that the native
	 * ret_from_exception performs.  The frame's epc is untouched
	 * (the dummy SC writes it back with the same value).
	 */
	__asm__ __volatile__ (
		"lr.d zero, (%0)\n"
		"sc.d zero, %1, (%0)"
		: : "r" (&regs->epc), "r" (regs->epc) : "memory");

	pvm_hypercall1(PVM_HC_PV_RETURN, (unsigned long)regs);
	(void)__flags;
	/* If the host returns here at all, it rejected the frame; the
	 * caller (pvm_pv_entry) parks.  Direct callers from entry.S
	 * also park via pvm_pv_return_rejected. */
}

/* E1: PV IRQ helper semantics assertion.  Runs only when PV mode is
 * active.  Verifies: enable -> read=enabled -> save returns old=enabled
 * and disables -> read=disabled -> restore -> read=enabled.  This
 * catches the "PV_IRQ_DISABLE clears before reading old" bug class. */
static int __init pvm_irq_state_test(void)
{
	unsigned long f1, f2;

	if (!pvm_pv_mode_active)
		return 0;
	/* post-boot regression: same cycle through the LOCAL helpers
	 * (which now go through the PV hypercalls when mode is on) */
	local_irq_enable();
	f1 = arch_local_save_flags();
	if (!(f1 & SR_IE)) {
		pr_err("PVM-IRQTEST: FAIL enable->read not set (0x%lx)\n", f1);
		return -1;
	}
	f1 = arch_local_irq_save();
	if (!(f1 & SR_IE)) {
		pr_err("PVM-IRQTEST: FAIL save lost old-enabled (0x%lx)\n", f1);
		return -1;
	}
	f2 = arch_local_save_flags();
	if (f2 & SR_IE) {
		pr_err("PVM-IRQTEST: FAIL still enabled after save (0x%lx)\n", f2);
		return -1;
	}
	arch_local_irq_restore(f1);
	f2 = arch_local_save_flags();
	if (!(f2 & SR_IE)) {
		pr_err("PVM-IRQTEST: FAIL not re-enabled after restore (0x%lx)\n", f2);
		return -1;
	}
	pr_info("PVM-IRQTEST: PASS (enable/save/read/restore cycle correct)\n");
	return 0;
}
late_initcall(pvm_irq_state_test);

static int pvm_stats_init(void)
{
	if (!pvm_guest_detected)
		return 0;
	pvm_stats_page = (u64 *)get_zeroed_page(GFP_KERNEL);
	if (!pvm_stats_page)
		return -ENOMEM;
	proc_create("pvm_stats", 0600, NULL, &pvm_stats_ops);
	pr_info("pvm-guest: stats window available at /proc/pvm_stats\n");
	return 0;
}
late_initcall(pvm_stats_init);

static int __init pvm_guest_init(void)
{
	unsigned long pvcs_pa, kernel_tp;
	long ret, host_version;

	if (!pvm_guest_detected)
		return 0;


	ret = pvm_hypercall1(PVM_HC_PROBE, PVM_ABI_VERSION);
	if (ret < 0) {
		/* Nothing is registered yet, no rollback needed. */
		pvm_no_host("PVM host probe", ret, false);
		return 0;
	}
	host_version = ret;
	if (host_version < PVM_ABI_VERSION) {
		pvm_no_host("PVM host version handshake", host_version, false);
		return 0;
	}
	if (pvm_csr_guest) {
		ret = pvm_hypercall0(PVM_HC_GET_FEATURES);
		if (ret < 0 || !(ret & PVM_FEATURE_CSR_TRAPS)) {
			pvm_no_host("CSR trap capability", ret, false);
			return 0;
		}
		if (pvm_pv_guest) {
			/* Full PV requested: the host must ALSO offer
			 * PV_EVENTS.  Failure is explicit (strict), no
			 * silent CSR fallback claiming PV. */
			if (!(ret & PVM_FEATURE_PV_EVENTS)) {
				pr_err("pvm-guest: pv_mode=pv but host lacks PV_EVENTS (features=0x%lx)\n", ret);
				panic("PVM: PV mode requested but not negotiated\n");
			}
			pr_info("pvm-guest: PV_EVENTS negotiated (features=0x%lx)\n", ret);
		}
	}

	/* TODO together with the host side: secondary harts register their
	 * PVCS, TP and event stacks through the CPU bringup path. */
	pvcs_pa = pvm_ptr_to_phys(this_cpu_ptr(&pvm_vcpu_struct));
	/*
	 * Initial kernel-side tp for this vCPU (on RV64 Linux tp holds
	 * current).  The host installs it when first entering the guest
	 * kernel context and then saves/restores whatever guest kernel
	 * value it observes; it must never pass through the interrupted
	 * guest user TLS tp.
	 */
	kernel_tp = (unsigned long)riscv_current_is_tp;
	ret = pvm_hypercall3(PVM_HC_REGISTER_PVCS,
		pvm_csr_guest ?
		  (pvm_pv_guest ? PVM_REGISTER_PV_EVENTS :
				  PVM_REGISTER_CSR_EVENTS) : 0,
		pvcs_pa, kernel_tp);
	if (ret) {
		/* REGISTER_PVCS itself failed, the host holds nothing. */
		pvm_no_host("PVCS registration", ret, false);
		return 0;
	}
	if (pvm_csr_guest) {
		if (pvm_pv_guest) {
			long r;
			/* IRQ helper self-test BEFORE enabling the global
			 * hook: directly call the hypercalls to verify
			 * save returns the old state, disable clears,
			 * restore re-enables.  Only after PASS do we set
			 * pvm_pv_mode_active=true. */
			{
				unsigned long f0, f1, f2;

				/* save the ORIGINAL SIE so we leave it intact */
				pvm_pv_irq_save(&f0);

				pvm_pv_irq_restore(SR_IE); /* enable */
				pvm_pv_irq_save(&f1);
				if (!(f1 & SR_IE)) {
					pr_err("pvm-guest: IRQ self-test FAIL save(old)\n");
					panic("PVM: IRQ self-test failed with pvm_mode=pv\n");
				}
				pvm_pv_irq_disable(&f1); /* atomic save+disable */
				if (!(f1 & SR_IE)) {
					pr_err("pvm-guest: IRQ self-test FAIL disable(old)\n");
					panic("PVM: IRQ self-test failed with pvm_mode=pv\n");
				}
				pvm_pv_irq_save(&f2);
				if (f2 & SR_IE) {
					pr_err("pvm-guest: IRQ self-test FAIL still-on\n");
					panic("PVM: IRQ self-test failed with pvm_mode=pv\n");
				}
				pvm_pv_irq_restore(f1); /* restore to enabled */
				pvm_pv_irq_save(&f2);
				if (!(f2 & SR_IE)) {
					pr_err("pvm-guest: IRQ self-test FAIL restore\n");
					panic("PVM: IRQ self-test failed with pvm_mode=pv\n");
				}
				/* restore the ORIGINAL state */
				pvm_pv_irq_restore(f0);
				pr_info("pvm-guest: IRQ helper self-test PASS\n");
			}
			/* register the PV entry (a0=entry only) */
			r = pvm_hypercall3(PVM_HC_PV_SET_ENTRY,
					   (unsigned long)pvm_pv_entry, 0, 0);
			if (r == 0) {
				pvm_pv_mode_active = true;
				pr_info("pvm-guest: PV entry registered (entry=0x%lx)\n",
					(unsigned long)pvm_pv_entry);
			} else {
				/* Requested PV mode with an explicit
				 * negotiation failure: panic, no silent
				 * CSR fallback claiming PV. */
				panic("PVM: PV entry rejected (%ld) with pvm_mode=pv\n", r);
			}
		}
		pr_info("PVM host registered (%s mode, ABI %ld): PVCS at 0x%lx, TP 0x%lx\n",
			pvm_pv_mode_active ? "PV slow-path" : "CSR trap",
			host_version, pvcs_pa, kernel_tp);
		return 0;
	}

	ret = pvm_hypercall2(PVM_HC_SET_EVENT_ENTRY, 0,
			     (unsigned long)pvm_event_entry);
	if (ret) {
		pvm_no_host("event entry registration", ret, true);
		return 0;
	}

	pr_info("PVM host registered (ABI %ld): PVCS at 0x%lx, TP 0x%lx, event entry 0x%lx\n",
		host_version, pvcs_pa, kernel_tp, (unsigned long)pvm_event_entry);
	return 0;
}
early_initcall(pvm_guest_init);

void __init pvm_early_setup(void)
{
	if (!pvm_detect()) {
		/* pvm=strict means a PVM host is mandatory: no marker, no boot. */
		if (pvm_strict)
			panic("PVM: no device-tree marker and pvm=strict is set\n");
		pr_info("no PVM host detected, booting natively\n");
		return;
	}

	pvm_guest_detected = true;
	if (pvm_csr_guest) {
		pr_info("experimental U-mode CSR trap host selected; registration is mandatory\n");
		return;
	}

	/*
	 * Hypercalls are issued later from pvm_guest_init(), once per-CPU
	 * areas are up.  TODO together with the RV64 host side:
	 *  - event handlers and the guest user return path
	 *  - switch irq/mmu related paths to the PV operations above
	 */
	pr_warn("PVM host detected, guest support incomplete\n");
}
