#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
set -euo pipefail
out=${1:?output directory required}
busybox=${2:?BUSYBOX must name a static RV64 busybox binary}
busybox=$(realpath "$busybox")
mkdir -p "$out"
cp tests/init "$out/init"
printf 'PVM-INIT-TEST-FILE\n' > "$out/test-file"
{
 for d in /bin /dev /tmp /proc /sys /etc; do printf 'dir %s 0755 0 0\n' "$d"; done
 printf 'nod /dev/console 0600 0 0 c 5 1\n'
 printf 'nod /dev/null 0666 0 0 c 1 3\n'
 printf 'file /bin/busybox %s 0755 0 0\n' "$busybox"
 printf 'file /bin/mmap-test %s/basic 0755 0 0\n' "$out"
 printf 'file /init %s/init 0755 0 0\n' "$out"
 printf 'file /etc/os-release-init-test %s/test-file 0644 0 0\n' "$out"
 for app in sh mount umount uname cat echo grep head ls dmesg poweroff sleep rm mkdir wait true false sync; do
  printf 'slink /bin/%s busybox 0777 0 0\n' "$app"
 done
} > "$out/initramfs.list"
"$out/gen_init_cpio" -t 0 "$out/initramfs.list" | gzip -n -9 > "$out/initramfs.cpio.gz"
