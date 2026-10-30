#!/bin/bash
# Stage-B host initramfs: busybox + vmm-linux + guest blobs.
set -euo pipefail
OUT=/mnt/u/hukeyang/USyscall/out/pvm-riscv64-20261029
W=${PVM_WORKDIR:-/tmp/pvm-linux-kvm}
B=${PVM_OUTDIR:-$W/stage-b}
export PATH="$OUT/toolroot/usr/bin:$PATH"
export LD_LIBRARY_PATH="$OUT/toolroot/usr/lib/x86_64-linux-gnu"
mkdir -p "$B/rootfs/bin" "$B/rootfs/proc" "$B/rootfs/dev"
cp ${PVM_GUEST_INITRAMFS:-/tmp/pvm-codex-final/accepted-1/initramfs.cpio.gz} "$B/guest-initramfs.cpio.gz"
cp $OUT/busybox-src/busybox "$B/rootfs/bin/"
for a in sh mount umount uname cat ls dmesg poweroff sleep grep tail; do
	ln -sf busybox "$B/rootfs/bin/$a"
done
K=$W/source/tools/testing/pvm-riscv64/kvm
cp "$K/vmm-linux" "$B/rootfs/pvm-vmm"
cp "$W/guest/arch/riscv/boot/Image" "$B/rootfs/guest-Image"
dtc -q -I dts -O dtb -o "$B/guest.dtb" "$K/guest.dts"
SZ=$(stat -c %s "$B/guest-initramfs.cpio.gz")
END=$((0x85000000 + SZ))
fdtput -t x "$B/guest.dtb" /chosen linux,initrd-end 0 "$(printf %x $END)"
echo "initrd-end=$(printf %x $END) (size $SZ)"
cp "$B/guest.dtb" "$B/rootfs/guest.dtb"
cp "$B/guest-initramfs.cpio.gz" "$B/rootfs/guest-initramfs.cpio.gz"
cat > "$B/rootfs/init" <<'INIT'
#!/bin/sh
export PATH=/bin
mount -t proc proc /proc
mount -t devtmpfs devtmpfs /dev 2>/dev/null
echo "=== STAGE-B HOST BOOT OK ==="
echo "=== STAGE-B GUEST RUN (console follows) ==="
/pvm-vmm
RC=$?
echo "=== STAGE-B VMM_RC=$RC ==="
echo "--- host dmesg PVM ---"
dmesg | grep -a "PVM_KVM" | tail -6
poweroff -f
INIT
chmod +x "$B/rootfs/init"
cd "$B/rootfs"
find . | cpio -o -H newc --quiet | gzip -9 > "$B/host-initramfs.cpio.gz"
sha256sum "$B/host-initramfs.cpio.gz"
