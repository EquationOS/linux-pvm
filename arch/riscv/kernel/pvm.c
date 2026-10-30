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
 * Status: an experimental M-mode monitor can bootstrap this kernel in
 * physical U-mode through CSR trap emulation (pvm,csr-guest). The Linux
 * KVM PVM vendor module does not exist yet. The synthetic PV frame path
 * remains scaffolding; ordinary S-mode boots without a marker stay native.
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

#include <asm/current.h>
#include <asm/ptrace.h>
#include <asm/pvm_para.h>

bool pvm_guest_detected __ro_after_init;
static bool pvm_csr_guest __ro_after_init;

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
	if (pvm_csr_guest)
		return true;
	np = of_find_compatible_node(NULL, NULL, "pvm,guest");
	of_node_put(np);

	return np != NULL;
}

/*
 * Dormant PV operations for the future host side, mirroring the pv_ops of
 * the x86_64 implementation.  They are intentionally not wired into the
 * live kernel paths yet: with no host to answer the ecalls, any of them
 * would trap into SBI/OpenSBI.
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

/*
 * Register this kernel with the PVM host:
 * PROBE (version handshake) -> REGISTER_PVCS (PVCS + kernel TP) ->
 * SET_EVENT_ENTRY.  This runs as an early_initcall, which is fine for
 * the S-mode scaffold but is NOT a U-mode boot entry: a U-mode guest
 * needs its own loader entry before any head.S/CSR work, see
 * Documentation/arch/riscv/pvm.rst.
 */
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
		pvm_csr_guest ? PVM_REGISTER_CSR_EVENTS : 0, pvcs_pa, kernel_tp);
	if (ret) {
		/* REGISTER_PVCS itself failed, the host holds nothing. */
		pvm_no_host("PVCS registration", ret, false);
		return 0;
	}
	if (pvm_csr_guest) {
		pr_info("PVM host registered (CSR trap mode, ABI %ld): PVCS at 0x%lx, TP 0x%lx\n",
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
