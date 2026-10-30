#!/bin/bash
# Stage-A host initramfs: static busybox + the PVM VMM + smoke payload.
# Built on NFS-6710E; artifacts under /tmp/pvm-linux-kvm/stage-a/.
set -e
OUT=/mnt/u/hukeyang/USyscall/out/pvm-riscv64-20261029
W=/tmp/pvm-linux-kvm
S=$W/stage-a
export PATH="$OUT/toolroot/usr/bin:$PATH"
export LD_LIBRARY_PATH="$OUT/toolroot/usr/lib/x86_64-linux-gnu"

mkdir -p $S/rootfs/bin $S/rootfs/proc $S/rootfs/sys $S/rootfs/dev $S/rootfs/tmp
cp $OUT/busybox-src/busybox $S/rootfs/bin/
for a in sh mount umount uname cat echo grep head ls dmesg poweroff sleep; do
	ln -sf busybox $S/rootfs/bin/$a
done
cp $W/source/tools/testing/pvm-riscv64/kvm/vmm $S/rootfs/pvm-vmm
cp $W/source/tools/testing/pvm-riscv64/kvm/payload.bin $S/rootfs/pvm-payload.bin

cat > $S/rootfs/init <<'EOF'
#!/bin/sh
export PATH=/bin
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev 2>/dev/null
echo "=== STAGE-A HOST BOOT OK ==="
uname -a
ls -la /dev/kvm || { echo "STAGE-A: FAIL no /dev/kvm"; poweroff -f; }
echo "--- dmesg kvm ---"
dmesg | grep -iE "kvm|pvm" | head -8
echo "=== STAGE-A VMM RUN ==="
/pvm-vmm
RC=$?
echo "STAGE-A VMM_RC=$RC"
echo "--- dmesg kvm after ---"
dmesg | grep -iE "PVM_KVM" | head -8
echo "=== STAGE-A HOST DONE ==="
poweroff -f
EOF
chmod +x $S/rootfs/init
cd $S/rootfs
find . | cpio -o -H newc --quiet | gzip -9 > $S/host-initramfs.cpio.gz
echo "built $S/host-initramfs.cpio.gz"
sha256sum $S/host-initramfs.cpio.gz
