#!/bin/bash
# Stage-C acceptance. Strict: real QEMU exit code to file, VMM/NEG rc,
# no Oops/panic/emulation-failure lines, guest assertion names counted
# inside the ACCEPTANCE BEGIN..END block (negative-suite PASS lines
# cannot mask missing guest assertions). Parameterized output dir.
set -euo pipefail
OUTDIR_TOOLROOT=${PVM_TOOLROOT_DIR:-/mnt/u/hukeyang/USyscall/out/pvm-riscv64-20261029}
W=${PVM_WORKDIR:-/tmp/pvm-linux-kvm}
C=${PVM_OUTDIR:-${1:-$W/stage-c}}
mkdir -p "$C"

set +e
timeout ${PVM_TIMEOUT:-420} qemu-system-riscv64 -M virt \
  -cpu rv64,h=false,sv48=false,sv57=false,sstc=false -smp 2 -m 2G \
  -nographic -no-reboot \
  -bios $OUTDIR_TOOLROOT/opensbi-1.3/build/platform/generic/firmware/fw_dynamic.bin \
  -kernel ${PVM_HOST_IMAGE:-$W/host/arch/riscv/boot/Image} \
  -initrd $C/host-initramfs.cpio.gz \
  -append "console=ttyS0 rdinit=/init" \
  > "$C/run.log" 2>&1
Q=$?
set -e
echo "$Q" > "$C/qemu.exit"

# count guest assertion lines strictly inside the guest ACCEPTANCE block
GP=$(sed -n '/ACCEPTANCE BEGIN/,/ACCEPTANCE END/p' "$C/run.log" | grep -ac ': PASS' || true)
GF=$(sed -n '/ACCEPTANCE BEGIN/,/ACCEPTANCE END/p' "$C/run.log" | grep -ac ': FAIL' || true)
echo "qemu_exit=$Q guest_block: PASS=$GP FAIL=$GF"

ok=1
[ "$Q" = 0 ] || { echo "MISS qemu_exit_0"; ok=0; }
[ "$GP" -ge 38 ] || { echo "MISS >=38 guest PASS in block (got $GP)"; ok=0; }
[ "$GF" = 0 ] || { echo "MISS guest zero FAIL (got $GF)"; ok=0; }
grep -aq "INIT-ALL-PASS" "$C/run.log" || { echo "MISS INIT-ALL-PASS"; ok=0; }
grep -aq "INIT-TESTS-ALL-PASS" "$C/run.log" || { echo "MISS INIT-TESTS-ALL-PASS"; ok=0; }
grep -aq "STAGE-C VMM_RC=0" "$C/run.log" || { echo "MISS VMM_RC=0"; ok=0; }
grep -aq "STAGE-C NEG_RC=0" "$C/run.log" || { echo "MISS NEG_RC=0"; ok=0; }
grep -aq "NEG: ALL-PASS" "$C/run.log" || { echo "MISS neg-suite"; ok=0; }
grep -aq "pvm-guest: PVM host registered" "$C/run.log" || { echo "MISS registered"; ok=0; }
grep -aq "PVM_KVM_SWITCH_OK.*SPP=0" "$C/run.log" || { echo "MISS SPP0"; ok=0; }
grep -aq "reboot: Power down" "$C/run.log" || { echo "MISS host-powerdown"; ok=0; }
if grep -aqE "Kernel panic|Oops:|BUG:|PVM_KVM emulation failed" "$C/run.log"; then
	echo "MISS no-panic/oops"; ok=0
fi
if [ "$ok" = 1 ]; then
	echo "STAGE-C: ACCEPT"
else
	echo "STAGE-C: REJECT"
fi
exit $((1 - ok))
