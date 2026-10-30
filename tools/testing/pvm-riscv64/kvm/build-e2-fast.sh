#!/bin/bash
# E2 build: fast-loop guest initramfs + host initramfs with the parent
# runner.  Uses the SAME guest kernel Image and normal PV DTB; only the
# guest initramfs differs (fast-loop instead of the 41-test suite).
set -euo pipefail
W=${PVM_WORKDIR:-/var/tmp/pvm-pv-dev-20261030}
K=$W/source/tools/testing/pvm-riscv64/kvm
C=${PVM_OUTDIR:-$W/e2-fast}
T=${PVM_TOOLROOT_DIR:-/mnt/u/hukeyang/USyscall/out/pvm-riscv64-20261029}
export PATH="$T/toolroot/usr/bin:$PATH"
export LD_LIBRARY_PATH="$T/toolroot/usr/lib/x86_64-linux-gnu"
make -C "$K" fast-loop-test fast-loop-parent vmm-linux vmm-neg payload-mmu.bin

mkdir -p "$C"

# --- guest fast-loop initramfs (minimal busybox + fast-loop-test) ---
G="$C/guest-fastloop"
rm -rf "$G"
mkdir -p "$G/bin" "$G/proc" "$G/dev" "$G/sys"
cp "$T/busybox-src/busybox" "$G/bin/"
for a in sh mount umount cat echo sleep; do ln -sf busybox "$G/bin/$a"; done
cp "$K/fast-loop-test" "$G/fast-loop-test"
chmod +x "$G/fast-loop-test"
cat > "$G/init" <<'GINIT'
#!/bin/sh
export PATH=/bin
mount -t proc proc /proc
mount -t devtmpfs devtmpfs /dev 2>/dev/null
mount -t sysfs sysfs /sys 2>/dev/null
echo "=== FASTLOOP GUEST BOOTED ==="
exec /fast-loop-test
GINIT
chmod +x "$G/init"
( cd "$G" && find . | cpio -o -H newc --quiet | gzip -9 > "$C/guest-fastloop.cpio.gz" )
echo "guest fastloop initramfs: $(sha256sum "$C/guest-fastloop.cpio.gz" | cut -c1-16)…"

# --- host initramfs (busybox + vmm-linux + fast-loop-parent + pvm-neg) ---
H="$C/host-rootfs"
rm -rf "$H"
mkdir -p "$H/bin" "$H/proc" "$H/dev"
cp "$T/busybox-src/busybox" "$H/bin/"
for a in sh mount umount cat ls dmesg poweroff sleep grep tail; do ln -sf busybox "$H/bin/$a"; done
cp "$K/vmm-linux" "$H/pvm-vmm"
cp "$K/vmm-neg" "$H/pvm-neg"
cp "$K/payload-mmu.bin" "$H/payload-mmu.bin"
cp "$K/fast-loop-parent" "$H/fast-loop-parent"
cp "$W/guest/arch/riscv/boot/Image" "$H/guest-Image"
dtc -q -I dts -O dtb -o "$H/guest.dtb" "$K/guest.dts"
fdtput -t s "$H/guest.dtb" /chosen bootargs "console=hvc0 earlycon=sbi pvm=strict pvm_mode=pv rdinit=/init"
cp "$C/guest-fastloop.cpio.gz" "$H/guest-initramfs.cpio.gz"
# fix initrd-end in the DTB from the actual size
SZ=$(stat -c %s "$C/guest-fastloop.cpio.gz")
END=$((0x85000000 + SZ))
fdtput -t x "$H/guest.dtb" /chosen linux,initrd-end 0 "$(printf %x $END)"
echo "initrd-end=$(printf %x $END) size=$SZ"

cat > "$H/init" <<'HINIT'
#!/bin/sh
export PATH=/bin
mount -t proc proc /proc
mount -t devtmpfs devtmpfs /dev 2>/dev/null
echo "=== E2 FAST TEST HOST BOOTED ==="
echo "--- running fast-loop parent (forks pvm-vmm, SIGTERM test) ---"
/fast-loop-parent
FRC=$?
echo "STAGE-E2-FAST RC=$FRC"
echo "--- fresh VM check ---"
/pvm-neg mmu
NRC=$?
echo "STAGE-E2-FRESHVM RC=$NRC"
dmesg | grep -a "PVM_KVM" | tail -6
echo "=== E2 FAST TEST DONE ==="
poweroff -f
HINIT
chmod +x "$H/init"
( cd "$H" && find . | cpio -o -H newc --quiet | gzip -9 > "$C/host-initramfs.cpio.gz" )
echo "host initramfs: $(sha256sum "$C/host-initramfs.cpio.gz" | cut -c1-16)…"
echo "BUILD OK"
