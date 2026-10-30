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

The RV64 branch has two deliberately separate event protocols:

* An experimental CSR trap bootstrap can run a single-hart Linux guest
  kernel and its processes in physical U-mode. A small M-mode monitor in
  ``tools/testing/pvm-riscv64`` emulates privileged instructions, builds
  host-owned Sv39 shadow roots, and injects native-style virtual supervisor
  traps. Linux's own ``entry.S`` builds ``pt_regs`` and returns through a
  trapped ``sret``. The DT compatible is ``pvm,csr-guest``. Registration is
  mandatory and negotiates ``PVM_FEATURE_CSR_TRAPS`` and
  ``PVM_REGISTER_CSR_EVENTS``; this mode does not use the synthetic frame
  entry or ``PVM_HC_RETU``.
* The original ``pvm,guest`` synthetic PV frame protocol still provides
  detection, registration/rollback and a fail-stop event-entry skeleton.
  Its handlers and live PV operation hooks remain incomplete. The M0
  integer protocol fixture does not constitute a Linux boot test.

There is still no RV64 Linux/KVM PVM host vendor module or production
switcher. The experimental M-mode monitor is a functional bootstrap
fixture, not that missing host implementation, and does not implement the
USyscall hardware fast path. Ordinary S-mode boots without a PVM DT marker
remain native. See ``tools/testing/pvm-riscv64/README.md`` for commands,
assertions, memory ownership, and explicit limitations.

Host registration
=================

``pvm_early_setup()`` (called from ``setup_arch()``) only scans the
device tree.  Hypercalls are issued later from ``pvm_guest_init()``
(``early_initcall``, once per-CPU areas are up):

  1. ``PVM_HC_PROBE`` — version/capability handshake.
  2. ``PVM_HC_REGISTER_PVCS`` with the GPA of the boot CPU's PVCS and
     the initial guest-kernel ``tp`` (on RV64 Linux ``tp`` holds
     ``current``).  Secondary harts are future work.
  3. ``PVM_HC_SET_EVENT_ENTRY`` with the address of
     ``pvm_event_entry``.

Failure paths roll back with ``PVM_HC_DEREGISTER``.  By default the
kernel then clears its detection state and continues natively — this
permissive fallback only exists so an S-mode kernel carrying a (test)
device-tree marker degrades to the verified native path; it is a
negative-test aid, not a runtime mode of PVM.  With ``pvm=strict`` on
the command line, any registration failure panics instead: a real
U-mode guest cannot fall back to running natively, so the future U-mode
boot path will always register strictly.

``early_initcall`` registration is not itself the U-mode boot entry. In
CSR trap mode the monitor enters ``head.S`` in physical U-mode and handles
privileged instructions from the beginning, before guest registration.
After PROBE, the guest queries GET_FEATURES and registers with the
CSR_EVENTS flag, without SET_EVENT_ENTRY. Host refusal always panics in
this mode, independently of the optional ``pvm=strict`` command line.
Native trap injection preserves all GPRs and SP; the guest's own virtual
sscratch/entry.S performs TP and stack selection. The monitor owns the
virtual kernel/user identity and routes user ecalls to the guest even if
a7 contains the host EID.

Host detection
==============

On x86_64 the guest detects PVM with a synthetic instruction sequence that
only traps when the kernel itself runs in CPL3.  RV64 has no equivalent
self-contained probe that is guaranteed trap-free on a native S-mode
kernel, so detection is based on the device tree instead: a PVM host for
RV64 must expose a top-level node with ``compatible = "pvm,guest"``.  When
no such node exists the guest boots natively. The separate experimental
CSR trap boot uses ``compatible = "pvm,csr-guest"`` instead.

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
``PVM_HC_RETU`` (see `Host registration`_).  PROBE, GET_FEATURES and registration are the initial handshake; other
PV operations require successful registration. Issued under OpenSBI they return
``SBI_ERR_NOT_SUPPORTED`` rather than crashing.
