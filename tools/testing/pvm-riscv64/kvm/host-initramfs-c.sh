#!/bin/bash
# Stage-C host initramfs. Parameterized: PVM_WORKDIR (source+builds),
# PVM_OUTDIR (this output), PVM_GUEST_INITRAMFS (the guest cpio.gz to
# embed). No hardcoded accepted-1 dependency.
set -euo pipefail
OUTDIR_TOOLROOT=/mnt/u/hukeyang/USyscall/out/pvm-riscv64-20261029
W=${PVM_WORKDIR:-/tmp/pvm-linux-kvm}
C=${PVM_OUTDIR:-${1:-$W/stage-c}}
GUEST_INITRD=${PVM_GUEST_INITRAMFS:-$W/stage-d/guest-initramfs-d.cpio.gz}
K=$W/source/tools/testing/pvm-riscv64/kvm
export PATH="$OUTDIR_TOOLROOT/toolroot/usr/bin:$PATH"
export LD_LIBRARY_PATH="$OUTDIR_TOOLROOT/toolroot/usr/lib/x86_64-linux-gnu"

[ -f "$K/vmm-neg" ] || { echo "missing $K/vmm-neg (make all first)" >&2; exit 1; }
[ -f "$GUEST_INITRD" ] || { echo "missing guest initramfs $GUEST_INITRD" >&2; exit 1; }

mkdir -p "$C/rootfs/bin" "$C/rootfs/proc" "$C/rootfs/dev"
cp $OUTDIR_TOOLROOT/busybox-src/busybox "$C/rootfs/bin/"
for a in sh mount umount uname cat ls dmesg poweroff sleep grep tail; do
	ln -sf busybox "$C/rootfs/bin/$a"
done
cp "$K/vmm-neg" "$C/rootfs/pvm-neg"
cp "$K/payload-c.bin" "$C/rootfs/payload-c.bin"
cp "$K/payload-loop.bin" "$C/rootfs/payload-loop.bin"
cp "$K/payload-d.bin" "$C/rootfs/payload-d.bin"
cp "$K/payload-mmu.bin" "$C/rootfs/payload-mmu.bin"
cp "$W/guest/arch/riscv/boot/Image" "$C/rootfs/guest-Image"
cp "$W/stage-b/guest.dtb" "$C/rootfs/guest.dtb"
cp "$GUEST_INITRD" "$C/rootfs/guest-initramfs.cpio.gz"
cp "$K/vmm-linux" "$C/rootfs/pvm-vmm"
cat > "$C/rootfs/init" <<'INIT'
#!/bin/sh
export PATH=/bin
mount -t proc proc /proc
mount -t devtmpfs devtmpfs /dev 2>/dev/null
echo "=== STAGE-C HOST BOOT OK ==="
echo "=== STAGE-C GUEST LINUX /init RUN ==="
/pvm-vmm
RC=$?
echo "STAGE-C VMM_RC=$RC"
echo "=== STAGE-C NEGATIVE SUITE ==="
/pvm-neg
NEGRC=$?
echo "STAGE-C NEG_RC=$NEGRC"
echo "=== STAGE-D MMU PAYLOAD ==="
/pvm-neg mmu
MMURC=$?
echo "STAGE-D MMU_RC=$MMURC"
dmesg | grep -a "PVM_KVM" | tail -4
echo "=== STAGE-C HOST DONE ==="
poweroff -f
INIT
chmod +x "$C/rootfs/init"
cd "$C/rootfs"
find . | cpio -o -H newc --quiet | gzip -9 > "$C/host-initramfs.cpio.gz"
sha256sum "$C/host-initramfs.cpio.gz"
