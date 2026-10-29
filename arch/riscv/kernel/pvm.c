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
 * Status: this file provides guest-side scaffolding, i.e. host detection
 * and the hypercall transport.  The host side (the KVM PVM vendor module
 * for RISC-V) does not exist yet; the PV operations below are prepared but
 * not hooked into the live kernel paths, so a kernel built with
 * CONFIG_PVM_GUEST=y still boots natively (S-mode under OpenSBI or on real
 * hardware) and only ever executes the detection path.
 *
 * Copyright (C) 2020 Ant Group
 * RISC-V port for the USyscall project, 2026.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */
#define pr_fmt(fmt) "pvm-guest: " fmt

#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/percpu.h>

#include <asm/pvm_para.h>

bool pvm_guest_detected __ro_after_init;

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
	unsigned long flags = 0;

	if (satp & PVM_LOAD_PGTBL_FLAGS_ASID)
		flags |= PVM_LOAD_PGTBL_FLAGS_ASID;
	pvm_hypercall2(PVM_HC_LOAD_PGTBL, flags, satp);
}

void __init pvm_early_setup(void)
{
	if (!pvm_detect()) {
		pr_info("no PVM host detected, booting natively\n");
		return;
	}

	pvm_guest_detected = true;

	/*
	 * TODO together with the RV64 host side:
	 *  - register the per-CPU PVCS physical address with the host
	 *  - set up the event entry point and the return-to-user path
	 *  - switch irq/mmu related paths to the PV operations above
	 */
	pr_warn("PVM host detected, guest support incomplete\n");
}
