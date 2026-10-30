# Experimental RV64 PVM Linux/KVM host

This is the ordinary software PVM baseline: an S-mode Linux host uses
`/dev/kvm` to run a guest Linux kernel and its processes in physical U-mode.
It requires neither the RISC-V H extension nor USyscall. OpenSBI boots the
host only; the guest does not use the sibling M-mode monitor.

Select `CONFIG_KVM=y`, `CONFIG_RISCV_PVM_HOST=y`, `CONFIG_FPU=y` in the
host. Leave `RISCV_PVM_HOST` disabled to select the existing H backend.
The guest uses `CONFIG_PVM_GUEST=y`, the `pvm,csr-guest` DT marker,
`CONFIG_HVC_RISCV_SBI=y`, and strict registration. Guest ecalls
are classified using host-owned virtual privilege, so user syscalls go
back to Linux and cannot invoke kernel hypercalls by forging a7.

Three guest/host mode combinations are supported: CSR trap (default),
PV slow path (`pvm_mode=pv` in the guest bootargs, host C handles
every PV entry/return), and PV fast gate (same guest, host parameter
`kvm.pv_fast=1` explicitly). The fast and slow paths use the same
guest Image and the same PV wiring; only the host-side gate is
switched. `kvm.pv_fast` defaults to 0.

## PV mode details

In `pvm_mode=pv` the guest registers a dedicated PV entry
(`PVM_HC_PV_SET_ENTRY`, a0=entry). User ecalls are dispatched into
`do_trap_ecall_u` via that entry with kernel TP/GP/SP and a0=pt_regs;
the host C path re-validates the 288-byte frame (page-by-page,
kernel-virtual U=0, R|W, in memslot, no reserved gate/state VAs) on
every call. Guest IRQ helpers (`irqflags.h`), satp writes
(`mm/context.c`) and TLB flushes (`tlbflush.h` including the ASID
variants) use dedicated hypercalls only when PV mode is active; native
and CSR-trap boots execute the original CSR instructions.

The fast gate (~1492 bytes of supervisor-only trampoline, U=0 in every
guest root) additionally handles user-ecall entry, PV_RETURN, and IRQ
helper FIDs 16–18 in assembly without returning to host C. It uses a
host-prepared per-task frame cache (re-validated on every C exit, guest
satp/sfence/SUM/MXR change or task switch; single-page 288-byte frames
only; cross-page falls back to C). Guest FP registers and fcsr stay
live across fast traps. All real host interrupts unconditionally exit
to C for signal/invalidation/timer/scheduling checks.

## Reproduce the bounded acceptance

The full configurations, source/artifact hashes, independent server
results and commands are maintained in the parent USyscall repository:
`Docs/checks/pvm-riscv64-pv-baseline.zh.md` (stages D/E1/E2) with
per-stage attachments. Build on server-local storage when the checkout
lives on NFS.

The staging layout is:

```
$PVM_WORKDIR/source/          # this Linux tree
$PVM_WORKDIR/host-obj/        # fresh O= host build
$PVM_WORKDIR/guest-obj/       # fresh O= guest build
$PVM_WORKDIR/stage-b/guest.dtb
$PVM_GUEST_INITRAMFS          # built by build-guest-initramfs-d.sh
$PVM_WORKDIR/guest/           # symlink to guest-obj for the packagers
$PVM_OUTDIR/                  # per-run host rootfs, archive and output
```

Cross-build the VMMs and fixtures with `make -C .../kvm all` (override
`CROSS` if needed). Compile `guest.dts` with dtc, then set the two-cell
`/chosen/linux,initrd-end` property to `0x85000000 + actual initrd bytes`
with fdtput. `host-initramfs-c.sh` packages the functional suite; the
parent report supplies the three-mode QEMU commands and strict verifier. The packaging scripts use the
shared toolroot for GCC, static BusyBox and OpenSBI 1.3. Set `PVM_WORKDIR` and `PVM_OUTDIR` for packaging and `PVM_HOST_IMAGE`
for the fast-loop runner. The normal suite additionally takes
`PVM_GUEST_INITRAMFS`. Redirect QEMU stdin from `/dev/null`.

### Fast-loop SIGTERM test

`build-e2-fast.sh` builds two archives: a minimal guest initramfs that
execs `/fast-loop-test` (warmup 1000 getpid with PID validation, prints
`FASTLOOP: READY`, then an infinite raw getpid loop with no signal
handler), and a host initramfs containing `/pvm-vmm`, `/pvm-neg` and
`/fast-loop-parent`. The parent forks and execs `/pvm-vmm`, pipes and
forwards its console, polls for the guest READY marker with a bounded
deadline, monitors ~100 ms for failures, then SIGTERMs the *host VMM
process* (not the guest). `waitpid` must report the child died by
SIGTERM. The host `/init` then runs `/pvm-neg mmu` to create and run a
fresh VM. `run-e2-fast.sh` boots QEMU with `kvm.pv_fast=1` and
strictly checks: QEMU exit 0, parent ALL-PASS, both RC=0 lines,
child-signaled PASS, guest READY, clean powerdown, no panic/Oops/FAIL,
and `PVM_GATE_FINAL` showing fast entry count greater than 1000 with
exact physical-trap partitioning. The kernel prints `PVM_GATE_FINAL` fast counter
totals on vCPU destroy for the acceptance record.

## Implemented boundary

Single guest vCPU, Sv39 host/guest, RV64IMAFDC, one fixed 126 MiB memslot
at GPA `[0x80200000,0x88000000)`, SBI DBCN console, TIME and SRST.
Private shadow roots enforce virtual kernel/user permissions. GPRs,
32 double FP registers, fcsr, scounteren and senvcfg are preserved across
the switch. Host IRQs, scheduling and signals remain active.

Memslot backing is pinned at first run; later slot changes are rejected.
MMU invalidation stops the VM. This is not elastic memory reclamation,
migration or complete dirty logging. The limited ONE_REG interface and
small VMM are not a complete replacement for generic QEMU's RISC-V KVM
CPU/register ABI. Guest SMP, RVV, broad ISA/device support, production
security review, and hardware performance evaluation remain outside this
acceptance. Native/H-backend compatibility has not been re-verified as
a full matrix. The synthetic PV-frame/RETU protocol remains separate
and incomplete. FP arithmetic passing does not establish a complete FP
state isolation matrix. No hardware acceleration claims are made.
