.. SPDX-License-Identifier: GPL-2.0

==========================
PVM guest support on RV64
==========================

PVM (Pagetable-based Virtual Machine) is a software-only hypervisor design
where the guest kernel runs in unprivileged mode (CPL3 on x86_64, U-mode on
RV64) under the host kernel, and privileged operations trap into the host
which dispatches them back into paravirtualized guest handlers.  The
original implementation is x86_64 only; see
``Documentation/virt/kvm/x86/pvm-spec.rst`` for the host ABI and
``arch/x86/kernel/pvm.c`` for the x86_64 guest.

This document describes the RV64 guest-side port under
``arch/riscv/kernel/pvm.c`` (``CONFIG_PVM_GUEST``).

Status
======

The RV64 port currently provides guest-side scaffolding:

  * device-tree based host detection,
  * the hypercall transport (see below),
  * the per-VCPU communication structure (PVCS) layout,
  * host registration (PVCS and event entry, with a fail-safe native
    fallback when no host answers),
  * an event entry point (``pvm_event_entry``) and dispatch skeleton,
  * dormant PV operations (TLB flush, page-table switch, halt) mirroring
    the x86_64 ``pv_ops`` set.

The host side for RV64 (the KVM PVM vendor module and the switcher) does
not exist yet.  Consequently the PV operations are not hooked into the
live kernel paths: a kernel built with ``CONFIG_PVM_GUEST=y`` boots
natively (S-mode under OpenSBI or on real hardware).  It executes the
detection path and, when a device-tree marker claims a PVM host is
present, attempts registration once; under OpenSBI the unknown-EID ecall
returns ``SBI_ERR_NOT_SUPPORTED`` and the kernel continues natively.
Functional event handlers, the guest-user return path, and relocating
the kernel image into a host-provided address range are future work to
be done together with the host side.

Host registration
=================

``pvm_early_setup()`` (called from ``setup_arch()``) only scans the
device tree.  Hypercalls are issued later from ``pvm_guest_init()``
(``early_initcall``, once per-CPU areas are up):

  1. ``PVM_HC_REGISTER_PVCS`` with the physical address of the boot
     CPU's PVCS.  Secondary harts are future work.
  2. ``PVM_HC_SET_EVENT_ENTRY`` with the address of
     ``pvm_event_entry``.

Any refusal clears the detection state and the kernel continues
natively; the event entry is never executed without a host.

Host detection
==============

On x86_64 the guest detects PVM with a synthetic instruction sequence that
only traps when the kernel itself runs in CPL3.  RV64 has no equivalent
self-contained probe that is guaranteed trap-free on a native S-mode
kernel, so detection is based on the device tree instead: a PVM host for
RV64 must expose a top-level node with ``compatible = "pvm,guest"``.  When
no such node exists the guest boots natively.

Hypercall ABI
=============

A hypercall is an ``ecall`` from U-mode that traps into the host (the
two-level ecall model).  Register usage mirrors the SBI calling
convention so a future host can reuse an SBI-style dispatcher:

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
the x86_64 segment/MSR entries have no RV64 counterpart.  The RV64-only
additions are ``PVM_HC_REGISTER_PVCS``, ``PVM_HC_SET_EVENT_ENTRY`` and
``PVM_HC_RETU`` (see `Host registration`_).  Hypercalls must only be
issued after successful registration; issued under OpenSBI they return
``SBI_ERR_NOT_SUPPORTED`` rather than crashing.
