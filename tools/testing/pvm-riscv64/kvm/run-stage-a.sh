#!/bin/bash
# Stage-A run: S-mode Linux host under OpenSBI with h=false (no H
# extension anywhere), then /dev/kvm runs the physical U-mode smoke
# payload. Executed on NFS-6710E. Log: /tmp/pvm-linux-kvm/stage-a/run.log
set -euo pipefail
OUT=/mnt/u/hukeyang/USyscall/out/pvm-riscv64-20261029
W=/tmp/pvm-linux-kvm
timeout 300 qemu-system-riscv64 -M virt -cpu rv64,h=false,sv48=false,sv57=false,sstc=false -smp 2 -m 2G \
  -nographic -no-reboot \
  -bios $OUT/opensbi-1.3/build/platform/generic/firmware/fw_dynamic.bin \
  -kernel $W/host/arch/riscv/boot/Image \
  -initrd $W/stage-a/host-initramfs.cpio.gz \
  -append "console=ttyS0 rdinit=/init" \
  > $W/stage-a/run.log 2>&1
Q=$?
echo "QEMU_EXIT=$Q"
exit 0
