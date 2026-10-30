# Experimental RV64 PVM U-mode Linux bootstrap

This test monitor runs one Linux kernel and its processes in **physical
U-mode**, without the RISC-V H extension. The monitor runs in M-mode. It
is a bounded functional bootstrap, not the still-missing Linux/KVM PVM
host backend, a production isolation implementation, or a USyscall fast
path. No performance claim follows from its TCG timings.

The guest must have CONFIG_PVM_GUEST=y and CONFIG_BLK_DEV_INITRD=y.
Requirements: RV64 Linux cross GCC/binutils, host GCC, Python 3, gzip,
dtc/fdtput, QEMU system RISC-V (validated with 11.0.2), and a static RV64
BusyBox with sh, mount, uname, cat, grep, dmesg, sync and poweroff.

From this directory:

```sh
make OUT=/absolute/run-1 CROSS_COMPILE=riscv64-linux-gnu- all initramfs \
  BUSYBOX=/absolute/path/to/static-rv64-busybox
bash run.sh /absolute/run-1 /absolute/path/to/arch/riscv/boot/Image 120
```

Use a different OUT for every concurrent experiment. Build the kernel
from the same source snapshot with its normal RISC-V build instructions;
`riscv defconfig` plus CONFIG_PVM_GUEST=y is a suitable starting point.
On a shared NFS mount, copy a source snapshot to server-local storage
before building: attribute caches can make an immediate `make` reuse an
old executable. Record source and artifact hashes with the result.

`run.sh` uses `-cpu rv64,h=false,sv48=false,sv57=false,sstc=false`, 128 MiB
RAM, one hart, a custom raw BIOS, and no OpenSBI. It creates a DT with
`pvm,csr-guest`, excludes monitor memory from guest RAM, and disables
PCI, the RTC and syscon shutdown/reboot devices. SBI TIME and SRST are
implemented by the monitor. The UART, PLIC and empty virtio-MMIO slots
are mapped directly for this fixture. Disk, network and external device
operation have not been validated.

The verifier requires all of: QEMU exit 0, mandatory CSR host
registration, a real `/init` start, zero failed program assertions,
completion markers, guest shutdown, and nonzero physical-U trap,
virtual-user entry and timer-delivery counts. The suite checks shell
programs, mmap, COW, fork/wait, RO-write SIGSEGV, SIGUSR1, nanosleep,
integer/FP arithmetic, user CSR SIGILL, rejected forged host ecall and
unmapped host memory. Expected child SIGSEGV/SIGILL diagnostic logs are
not kernel Oopses. `INIT-ALL-PASS` is emitted only after every shell and
C-suite check succeeds; a clean shutdown alone is insufficient.

For negative controls, use a fresh OUT with
`MONITOR_CFLAGS=-DPVM_TEST_NO_CSR_FEATURE`: mandatory registration must
panic before `/init`, and run.sh must fail. With a normal monitor,
`PVM_TEST_EXTRA_ARGS=pvm.test_fail=1 bash run.sh ...` must also fail even
though the guest powers off cleanly. Those are test-only controls.

## Execution and ownership

Every normal monitor exit forces real mstatus.MPP=U, MPRV/MIE off; every
trap checks the saved MPP is U. Virtual supervisor/user identity is a
private monitor variable changed only by virtual trap/sret transitions,
not a guest-supplied hypercall number. Virtual CSRs preserve read-before-
write semantics; native supervisor traps preserve GPRs/SP and update
SPP/SPIE/SIE. FP registers remain untouched by this integer-only monitor;
Linux's own FP save/restore routines handle process state.

Memory at 0x80000000..0x80100000 is monitor-only. The read-only PMP grant
at 0x80100000..0x80200000 permits hardware page-table walks of host-owned
shadow tables; these pages are excluded from guest virtual mappings.
PMP grants guest RAM at 0x80200000..0x88000000. The DT excludes the first
2 MiB from Linux memblock. Guest huge leaves that cross ownership bounds
are split; no shadow leaf exposes monitor backing. Virtual supervisor
and user roots filter guest U permissions separately; SUM transitions,
SATP and sfence rebuild the shadow. Shadow A/D bits are eager; precise
guest dirty/access tracking and reclamation are not implemented.

## Limits

Only Sv39, one hart and this fixed RAM/device layout are supported. WFI
currently polls rather than scheduling a host vCPU. The shadow is rebuilt
conservatively, including virtual privilege transitions; this is slow.
There is no host Linux process/vCPU API, SMP, migration, revocation,
balloon/reclaim integration, RVV, asynchronous device virtualization or
complete architectural CSR emulation. CSR registers not implemented by
the fixture are not a promise of a complete CPU model. The guest kernel
is trusted for the bootstrap; adversarial guest-kernel isolation and
all MM corner cases have not been audited. The synthetic PV-frame ABI
is not exercised by this CSR mode. USyscall needs a separate switch-root
adapter and a solution for high guest virtual entry addresses.
