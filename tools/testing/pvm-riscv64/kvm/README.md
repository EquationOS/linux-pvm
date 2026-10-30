# Experimental RV64 PVM Linux/KVM host

This is the ordinary software PVM baseline: an S-mode Linux host uses
`/dev/kvm` to run a guest Linux kernel and its processes in physical U-mode.
It requires neither the RISC-V H extension nor USyscall. OpenSBI boots the
host only; the guest does not use the sibling M-mode monitor.

Select `CONFIG_KVM=y`, `CONFIG_RISCV_PVM_HOST=y`, `CONFIG_FPU=y` in the
host. Leave `RISCV_PVM_HOST` disabled to select the existing H backend.
The guest uses `CONFIG_PVM_GUEST=y`, the `pvm,csr-guest` DT marker,
`CONFIG_HVC_RISCV_SBI=y`, and strict CSR-mode registration. Guest ecalls
are classified using host-owned virtual privilege, so user syscalls go
back to Linux and cannot invoke kernel hypercalls by forging a7.

## Reproduce the bounded acceptance

The full configurations, source/artifact hashes, independent server
results and commands are maintained in the parent USyscall repository:
`Docs/checks/pvm-riscv64-linux-kvm-host/`. Its `reproduce.zh.md` starts
from an archived source tree and fresh host/guest object directories.
Build on server-local storage when the checkout lives on NFS.

The staging layout is:

```
$PVM_WORKDIR/source/          # this Linux tree
$PVM_WORKDIR/host/            # fresh O= host build
$PVM_WORKDIR/guest/           # fresh O= guest build
$PVM_WORKDIR/stage-b/guest.dtb
$PVM_GUEST_INITRAMFS          # built by ../Makefile initramfs
$PVM_OUTDIR/                 # per-run host rootfs, archive and output
```

Cross-build the VMMs and fixtures with `make -C .../kvm all` (override
`CROSS` if needed). Compile `guest.dts` with dtc, then set the two-cell
`/chosen/linux,initrd-end` property to `0x85000000 + actual initrd bytes`
with fdtput. Run `host-initramfs-c.sh`, then `run-stage-c.sh`.
The packaging scripts currently use the shared toolroot
`/mnt/u/hukeyang/USyscall/out/pvm-riscv64-20261029` for GCC, static BusyBox
and OpenSBI 1.3. The runner also accepts `PVM_TOOLROOT_DIR`,
`PVM_HOST_IMAGE`, and `PVM_TIMEOUT`. The exact environment is in the
parent reproduction guide. In noninteractive pipelines redirect the
runner's stdin from `/dev/null` so QEMU cannot consume shell input.

Stage A runs a minimal physical-U payload. Stage B boots Linux `/init`.
Stage C repeats Linux's 38 shell/C assertions and runs host controls:
wrong GPA, unsupported flags and a second memslot must fail; a child
VMM reports readiness through DBCN write_byte, then resumes into a U-mode
loop. A bounded pipe poll precedes SIGTERM, exact signal-status checking
and successful creation/execution of another VM. The final payload
also crosses an instruction-fetch page boundary and checks WFI wake
with virtual global SIE clear and local STIE enabled.

The runner checks real QEMU/VMM/negative-suite status, guest assertions
inside the acceptance block, registration, physical SPP=0, shutdown and
absence of kernel panic/Oops/emulation failures. Guest
`pvm.test_fail=1` must be rejected even when QEMU exits successfully.

## Implemented boundary

Single guest vCPU, Sv39 host/guest, RV64IMAFDC, one fixed 126 MiB memslot
at GPA `[0x80200000,0x88000000)`, SBI DBCN console, TIME and SRST.
The supervisor-only trampoline restores the host root/context;
private shadow roots enforce virtual kernel/user permissions. GPRs,
32 double FP registers, fcsr, scounteren and senvcfg are preserved across
the switch. Host IRQs, scheduling and signals remain active.

Memslot backing is pinned at first run; later slot changes are rejected.
MMU invalidation stops the VM. This is not elastic memory reclamation,
migration or complete dirty logging. The limited ONE_REG interface and
small VMM are not a complete replacement for generic QEMU's RISC-V KVM
CPU/register ABI. Guest SMP, RVV, broad ISA/device support, production
security review, hardware execution and performance evaluation remain
outside this acceptance. FP arithmetic passing does not establish a
complete FP state isolation matrix. The synthetic PV-frame/RETU protocol
remains separate and incomplete; this backend uses CSR trap events.
