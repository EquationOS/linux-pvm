#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Build the guest archive from source; gen_init_cpio creates device entries
# without requiring mknod privileges. Extra tests share /init's FAILS guard.
set -euo pipefail
W=${PVM_WORKDIR:-/tmp/pvm-linux-kvm}
T=${PVM_TOOLROOT_DIR:-/mnt/u/hukeyang/USyscall/out/pvm-riscv64-20261029}
K=$W/source/tools/testing/pvm-riscv64/kvm
A=${PVM_GUEST_ASSETS:-$W/guest-assets-d}
OUT=${PVM_GUEST_INITRD_OUT:-$W/stage-d/guest-initramfs-d.cpio.gz}
export PATH="$T/toolroot/usr/bin:$PATH"
export LD_LIBRARY_PATH="$T/toolroot/usr/lib/x86_64-linux-gnu"
mkdir -p "$A" "$(dirname "$OUT")"
make -C "$W/source/tools/testing/pvm-riscv64" OUT="$A" BUSYBOX="$T/busybox-src/busybox" initramfs
python3 - "$A/init" <<'PY'
import pathlib,sys
p=pathlib.Path(sys.argv[1]); s=p.read_text()
mark='echo "=== PVM-RISCV64 U-MODE /INIT ACCEPTANCE END ==="'
assert s.count(mark)==1
extra='''echo "=== STAGE-D FP/SIGNAL TEST ==="
/fp-signal-test
FPRC=$?
echo "STAGE-D FP_RC=$FPRC"
[ "$FPRC" = 0 ] || fail fp-fcsr-suite
echo "=== STAGE-D GETPID WINDOW ==="
/getpid-window 10000
GPRC=$?
echo "STAGE-D GP_RC=$GPRC"
[ "$GPRC" = 0 ] || fail getpid-window
if [ "$FAILS" = 0 ]; then echo "STAGE-D-GUEST: ALL-PASS"; fi
'''
p.write_text(s.replace(mark,extra+mark))
PY
for prog in getpid-window fp-signal-test; do
    test -f "$K/$prog"
    printf 'file /%s %s/%s 0755 0 0\n' "$prog" "$K" "$prog" >> "$A/initramfs.list"
done
"$A/gen_init_cpio" -t 0 "$A/initramfs.list" | gzip -n -9 > "$OUT"
sha256sum "$OUT"
