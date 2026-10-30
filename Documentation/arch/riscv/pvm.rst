.. SPDX-License-Identifier: GPL-2.0

===================
PVM support on RV64
===================

PVM (Pagetable-based Virtual Machine) is a software-only hypervisor design
where the guest kernel runs in unprivileged mode (CPL3 on x86_64, U-mode on
RV64) under the host kernel, and privileged operations trap into the host
which dispatches them back into paravirtualized guest handlers.  The
original implementation is x86_64 only; see
``Documentation/virt/kvm/x86/pvm-spec.rst`` for the host ABI and
``arch/x86/kernel/pvm.c`` for the x86_64 guest.

This document describes the RV64 guest-side port under
``arch/riscv/kernel/pvm.c`` (``CONFIG_PVM_GUEST``) and the S-mode Linux
KVM host under ``arch/riscv/kvm/pvm`` (``CONFIG_RISCV_PVM_HOST``).

Modes
=====

The RV64 branch has three deliberately separate event paths:

**CSR trap mode** (``pvm_mode=csr``, default).  A single-hart Linux
guest kernel and its processes run in physical U-mode.  The S-mode Linux
host (or the separate M-mode monitor in ``tools/testing/pvm-riscv64``)
emulates privileged instructions, builds host-owned Sv39 shadow roots,
and injects native-style virtual supervisor traps.  Linux's own
``entry.S`` builds ``pt_regs`` and returns through a trapped ``sret``.
The DT compatible is ``pvm,csr-guest``.  Registration is mandatory and
negotiates ``PVM_FEATURE_CSR_TRAPS`` and ``PVM_REGISTER_CSR_EVENTS``;
this mode does not use the synthetic frame entry or ``PVM_HC_RETU``.

**PV slow-path mode** (``pvm_mode=pv``).  The same guest kernel adds
paravirtualized syscall entry/return and explicit IRQ/MMU hooks on top
of CSR trap delivery for asynchronous events.  The guest registers with
``PVM_REGISTER_PV_EVENTS``, negotiates ``PVM_FEATURE_PV_EVENTS``, and
installs a dedicated PV entry via ``PVM_HC_PV_SET_ENTRY`` (a0 = entry
GVA; a1 = a2 = 0).  User-mode ecalls are dispatched directly into the
guest kernel's PV entry (a0 = pt_regs pointer, kernel TP/GP/SP
installed) which calls ``do_trap_ecall_u`` with complete Linux syscall
semantics — there is no syscall whitelist.  On return,
``ret_from_exception``'s PV branch issues ``PVM_HC_PV_RETURN`` (a0 =
pt_regs GVA) after all native user-return maintenance (TASK_TI_KERNEL_SP,
SCS, sscratch) has run. The dedicated PV syscall return wrapper also
restores the saved kernel SP and clears the load reservation. The tested
configuration does not enable shadow call stacks or vector state. The host C slow path re-validates the full
288-byte frame page by page (kernel-virtual U=0, R|W, within the
memslot, no reserved VAs) on every call.  A user-mode forgery of
PV_RETURN is a plain user ecall delivered to the guest kernel; it
cannot elevate identity.

**PV fast gate** (same ``pvm_mode=pv`` guest, host ``kvm.pv_fast=1``).
When the parameter is explicitly set (default 0), an S-mode assembly
gate in the supervisor-only trampoline page (~1492 bytes, U=0 in every
guest root) handles the three hot events without returning to host C:
(a) user ecalls — build the pt_regs in the per-task kernel frame,
switch to the kernel shadow root, sret into the PV entry; (b) supervisor
PV_RETURN — validate the frame GVA/TP match against the host-private
cache, read the entire frame under the bridge root, restore user GPRs
and PC, sret to user; (c) supervisor IRQ helpers (FIDs 16–18) — read,
save-and-clear, or restore the virtual SIE directly.  All real host
interrupts unconditionally exit to C, which checks signals, memslot
invalidation, timer and scheduling.  Guest FP registers and fcsr stay
live across fast traps; the frame's FS/SD/UXL derive from the actual
trap state.  Syscall/return candidates fall back to C on SUM/MXR differences,
cross-page frames, cache invalidation or validation mismatch. IRQ
helpers can remain on the gate without switching roots.  ``kvm.pv_fast=0`` (the default) keeps the same
guest on the pure C slow path.

The original ``pvm,guest`` synthetic PV frame protocol retains
detection, registration rollback and a fail-stop event-entry skeleton.
Its handlers and live PV operation hooks remain incomplete.

Guest PV hooks
==============

When ``pvm_pv_mode_active`` is set (only after successful PV
negotiation, which includes an IRQ-helper self-test before the flag is
enabled), the following guest-side paths use dedicated hypercalls:

* ``irqflags.h``: ``arch_local_save_flags``, ``arch_local_irq_enable``,
  ``arch_local_irq_disable``, ``arch_local_irq_save`` and
  ``arch_local_irq_restore`` use ``PVM_HC_PV_IRQ_SAVE`` (16),
  ``PVM_HC_PV_IRQ_RESTORE`` (17) and ``PVM_HC_PV_IRQ_DISABLE`` (18) via
  ``asm/pvm_hypercall.h`` (a minimal-dependency header with constants-only UAPI
  dependencies, guarded by ``NO_ALTERNATIVE`` for the EFI libstub).
  ``PV_IRQ_DISABLE`` returns the old SIE state before clearing.
* ``mm/context.c``: both ``set_mm_asid`` and ``set_mm_noasid`` satp
  writes use ``PVM_HC_LOAD_PGTBL`` (a0 = flags reserved 0, a1 = satp).
* ``tlbflush.h``: ``local_flush_tlb_all``, ``local_flush_tlb_page``,
  ``local_flush_tlb_all_asid`` and ``local_flush_tlb_page_asid`` use
  ``PVM_HC_TLB_FLUSH`` (3) and ``PVM_HC_TLB_INVLPG`` (5); the ASID
  variants conservatively flush everything.

Native and CSR-trap boots do not execute these hooks (the flag is
false, the branches fall through to the original CSR instructions).

Host detection
==============

On x86_64 the guest detects PVM with a synthetic instruction sequence
that only traps when the kernel itself runs in CPL3.  RV64 has no
equivalent self-contained probe that is guaranteed trap-free on a
native S-mode kernel, so detection is based on the device tree instead:
a PVM host for RV64 must expose a top-level node with
``compatible = "pvm,csr-guest"`` (or ``"pvm,guest"`` for the synthetic
frame skeleton).  ``pvm_mode=pv`` on the kernel command line upgrades a
``pvm,csr-guest`` marker to PV mode; a negotiation failure with PV
requested is a hard panic, never a silent CSR fallback.  With no marker
the guest boots natively.

Host registration
=================

``pvm_early_setup()`` (called from ``setup_arch()``) scans the device
tree.  Hypercalls are issued later from ``pvm_guest_init()``
(``early_initcall``, once per-CPU areas are up):

  1. ``PVM_HC_PROBE`` — version/capability handshake.
  2. ``PVM_HC_GET_FEATURES`` — the host advertises
     ``PVM_FEATURE_CSR_TRAPS | PVM_FEATURE_PV_EVENTS``.
  3. ``PVM_HC_REGISTER_PVCS`` with the GPA of the boot CPU's PVCS and
     the initial guest-kernel ``tp``; PV mode uses the
     ``PVM_REGISTER_PV_EVENTS`` flag.
  4. ``PVM_HC_PV_SET_ENTRY`` (PV mode only) with the dedicated PV entry
     GVA.

Failure paths roll back with ``PVM_HC_DEREGISTER``.  With ``pvm=strict``
on the command line, any registration failure panics: a real U-mode
guest cannot fall back to running natively.  In PV mode, both an IRQ
self-test failure and a PV entry rejection are hard panics with the
reason printed.

Measurement windows
===================

``PVM_HC_STATS_RESET`` (12) records the current value of every exported
counter as the window base; ``PVM_HC_STATS_READ`` (13) writes a 16-slot
u64 delta snapshot to a dedicated guest-provided stats GPA (page-aligned,
not the PVCS page, bounds-checked).  Schema version 3 (slot 14):

===== =================================================================
0-15  user_ecall, sbi, hypercall, csr, sret, sfence, satp, fill,
      inj_fault, inj_irq, host_irq, vmm_exit, wfi, exits, schema(3),
      inj_ill
16-18 pv_entry, pv_return, pv_reject
19-25 phys_traps (asm-real), root_writes (asm-real), sfences
      (asm-real), fast_in, fast_out, fast_irq, fast_fallback
===== =================================================================

Fast-path events are folded into the categorical counters exactly once
each (a fast syscall counts as one user_ecall and one pv_entry; a fast
return as one pv_return, one user_entries and one hypercall).  In the fixed getpid acceptance window, the categorical sum equals total
physical traps, not C exits; C exits are reported separately. Schema 3
reports measured root writes; the schema 2 ``4*exits`` estimate remains
in historical results only.

Hypercall ABI
=============

A hypercall is an ``ecall`` from U-mode that traps into the host (the
two-level ecall model).  Register usage mirrors the SBI calling
convention:

===== =================================================================
a7    hypercall extension id, always ``PVM_HC_EID`` (0x70766D, "pvm")
a6    hypercall number (``PVM_HC_*``)
a0-a5 up to 6 arguments
a0    return value: 0 on success, negative errno on failure
a1    optional second return value
===== =================================================================

The arch-neutral hypercall numbers of the x86_64 ABI are kept
(``PVM_HC_LOAD_PGTBL``, ``PVM_HC_IRQ_WIN``, ``PVM_HC_IRQ_HALT``,
``PVM_HC_TLB_FLUSH``, ``PVM_HC_TLB_FLUSH_CURRENT``, ``PVM_HC_TLB_INVLPG``);
the x86_64 segment/MSR entries have no RV64 counterpart.  RV64-only
additions are ``PVM_HC_REGISTER_PVCS``, ``PVM_HC_SET_EVENT_ENTRY``,
``PVM_HC_RETU``, ``PVM_HC_STATS_RESET``, ``PVM_HC_STATS_READ``,
``PVM_HC_PV_RETURN``, ``PVM_HC_PV_SET_ENTRY``,
``PVM_HC_PV_IRQ_SAVE``, ``PVM_HC_PV_IRQ_RESTORE`` and
``PVM_HC_PV_IRQ_DISABLE``.  Issued under OpenSBI they return
``SBI_ERR_NOT_SUPPORTED`` rather than crashing.

Scope and limits
================

The S-mode host is limited to one guest vCPU, a fixed 126 MiB memslot
at GPA ``[0x80200000,0x88000000)``, Sv39, and a small VMM/ONE_REG
subset.  It does not implement the full generic QEMU/KVM RISC-V ABI,
guest SMP, RVV, migration or elastic reclamation.  Native and
H-extension backend compatibility has not been re-run as a full matrix.
This is not a production security or performance result; no hardware
performance conclusions are drawn from the software fast gate.  The
bounded acceptance evidence and reproduction instructions are maintained
in the parent USyscall repository at
``Docs/checks/pvm-riscv64-pv-baseline.zh.md``.
