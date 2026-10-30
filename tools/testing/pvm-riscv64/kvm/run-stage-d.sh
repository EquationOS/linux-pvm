#!/bin/bash
# Stage-D acceptance: boots the S-mode host, runs the D payload (D1
# correctness assertions) + the 38-assertion guest Linux suite +
# negative suite, then outputs machine-readable stats. Strict checks.
set -euo pipefail
OUTDIR_TOOLROOT=${PVM_TOOLROOT_DIR:-/mnt/u/hukeyang/USyscall/out/pvm-riscv64-20261029}
W=${PVM_WORKDIR:-/tmp/pvm-linux-kvm}
C=${PVM_OUTDIR:-${1:-$W/stage-d}}
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
grep -aq "STAGE-D: PASS" "$C/run.log" || { echo "MISS D-payload assertions"; ok=0; }
grep -aq "STAGE-MMU: PASS" "$C/run.log" || { echo "MISS MMU payload"; ok=0; }
grep -aq "STAGE-D-GUEST: ALL-PASS" "$C/run.log" || { echo "MISS guest D aggregate"; ok=0; }
grep -aq "FP-TESTS-PASS" "$C/run.log" || { echo "MISS FP signal/syscall test"; ok=0; }
grep -aq "STAGE-D FP_RC=0" "$C/run.log" || { echo "MISS FP_RC=0"; ok=0; }
grep -aq "STAGE-D GP_RC=0" "$C/run.log" || { echo "MISS getpid-window rc=0"; ok=0; }
# stats JSON: exact user_ecall == N+1 (N=10000; RESET's user ecall is
# in the base, READ's own write() user ecall is counted)
if ! grep -aq '"type":"pvm_stats"' "$C/run.log"; then
	echo "MISS stats JSON"; ok=0
else
	UE=$(grep -a '"type":"pvm_stats"' "$C/run.log" | tail -1 | sed 's/.*"user_ecall":\([0-9]*\).*/\1/')
	UC=$(grep -a '"type":"pvm_stats"' "$C/run.log" | tail -1 | sed 's/.*"user_ecall":\([0-9]*\).*/\1/')
	SC=$(grep -a '"type":"pvm_stats"' "$C/run.log" | tail -1 | sed 's/.*"schema":\([0-9]*\).*/\1/')
	echo "stats user_ecall=$UE schema=$SC"
	[ "$SC" = "2" ] || { echo "MISS schema==2"; ok=0; }
	# exact: N getpid + 1 (the write(read) user ecall; write(reset)'s is in base)
	[ -n "$UE" ] && [ "$UE" -eq 10001 ] || { echo "MISS user_ecall==10001 (got $UE)"; ok=0; }
fi
grep -aq "reboot: Power down" "$C/run.log" || { echo "MISS host-powerdown"; ok=0; }
if grep -aqE "Kernel panic|Oops:|BUG:|PVM_KVM emulation failed" "$C/run.log"; then
	echo "MISS no-panic/oops"; ok=0
fi

# extract stats window output if present (machine-readable JSON-lines)
grep -a '"type":"pvm_stats"' "$C/run.log" > "$C/stats.jsonl" || true

if [ "$ok" = 1 ]; then
	echo "STAGE-D: ACCEPT"
else
	echo "STAGE-D: REJECT"
fi
exit $((1 - ok))
