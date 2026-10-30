#!/bin/bash
# Stage-B run: S-mode host (Sv39-only, no H) boots guest Linux /init
# through /dev/kvm. Independent output dir; stage-A artifacts kept.
# Log: /tmp/pvm-linux-kvm/stage-b/run.log
set -euo pipefail
OUT=/mnt/u/hukeyang/USyscall/out/pvm-riscv64-20261029
W=${PVM_WORKDIR:-/tmp/pvm-linux-kvm}
B=$W/stage-b
mkdir -p "$B"
cp /tmp/pvm-codex-final/accepted-1/initramfs.cpio.gz "$B/guest-initramfs.cpio.gz"
timeout 600 qemu-system-riscv64 -M virt -cpu rv64,h=false,sv48=false,sv57=false,sstc=false -smp 2 -m 2G \
  -nographic -no-reboot \
  -bios $OUT/opensbi-1.3/build/platform/generic/firmware/fw_dynamic.bin \
  -kernel $W/host/arch/riscv/boot/Image \
  -initrd $B/host-initramfs.cpio.gz \
  -append "console=ttyS0 rdinit=/init" \
  > $B/run.log 2>&1
Q=$?
echo "QEMU_EXIT=$Q"
exit 0
