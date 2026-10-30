#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Usage: run.sh OUT Image [timeout-seconds]. Run each attempt in its own OUT.
set -euo pipefail
out=$(realpath "${1:?output directory}")
image=$(realpath "${2:?RV64 Linux Image}")
limit=${3:-120}
qemu=${QEMU:-qemu-system-riscv64}
cpu=rv64,h=false,sv48=false,sv57=false,sstc=false
"$qemu" -M "virt,dumpdtb=$out/guest.dtb" -cpu "$cpu" -smp 1 -m 128M -nographic 2>"$out/dtb.txt"
fdtput -t x "$out/guest.dtb" /memory@80000000 reg 0 80200000 0 7e00000
for node in /soc/pci@30000000 /soc/rtc@101000 /poweroff /reboot; do
 fdtput -t s "$out/guest.dtb" "$node" status disabled
done
fdtput -c "$out/guest.dtb" /pvm
fdtput -t s "$out/guest.dtb" /pvm compatible pvm,csr-guest
set +e
timeout "$limit" "$qemu" -M virt -cpu "$cpu" -smp 1 -m 128M -nographic \
 -monitor none -dtb "$out/guest.dtb" -bios "$out/monitor.bin" -kernel "$image" \
 -initrd "$out/initramfs.cpio.gz" \
 -append "console=ttyS0 earlycon=uart8250,mmio,0x10000000 rdinit=/init pvm=strict panic=1 loglevel=7 ${PVM_TEST_EXTRA_ARGS:-}" \
 >"$out/boot.log" 2>&1
status=$?
set -e
printf '%s\n' "$status" > "$out/qemu-status.txt"
python3 check-result.py "$out" "$status"
