#!/bin/bash
# Strictly verify interruption of a real Linux syscall loop under the gate.
set -euo pipefail
W=${PVM_WORKDIR:-/var/tmp/pvm-pv-dev-20261030}
C=${PVM_OUTDIR:-$W/e2-fast}
HOST_IMAGE=${PVM_HOST_IMAGE:-$W/host-obj/arch/riscv/boot/Image}
T=${PVM_TOOLROOT_DIR:-/mnt/u/hukeyang/USyscall/out/pvm-riscv64-20261029}
export PVM_WORKDIR="$W" PVM_OUTDIR="$C"
bash "$W/source/tools/testing/pvm-riscv64/kvm/build-e2-fast.sh" > "$C-build.txt" 2>&1
cmp "$W/guest/arch/riscv/boot/Image" "$C/host-rootfs/guest-Image"
set +e
timeout 180 qemu-system-riscv64 -M virt \
  -cpu rv64,h=false,sv48=false,sv57=false,sstc=false -smp 2 -m 2G \
  -nographic -no-reboot \
  -bios "$T/opensbi-1.3/build/platform/generic/firmware/fw_dynamic.bin" \
  -kernel "$HOST_IMAGE" -initrd "$C/host-initramfs.cpio.gz" \
  -append 'console=ttyS0 rdinit=/init kvm.pv_fast=1' \
  </dev/null > "$C/run.log" 2>&1
rc=$?
set -e
echo "$rc" > "$C/qemu.exit"
python3 - "$C" <<'PY'
import pathlib,re,json,sys
p=pathlib.Path(sys.argv[1]);s=(p/'run.log').read_text(errors='replace')
assert (p/'qemu.exit').read_text().strip()=='0'
for marker in ('FASTLOOP-HOST: ALL-PASS','STAGE-E2-FAST RC=0',
               'STAGE-E2-FRESHVM RC=0','child-signaled PASS',
               'FASTLOOP: READY','STAGE-MMU: PASS','reboot: Power down'):
    assert marker in s,marker
assert not re.search(r'Kernel panic|Oops:|FASTLOOP[^\n]*FAIL|PVM_KVM emulation failed',s)
rows=re.findall(r'PVM_GATE_FINAL fast=(\d+) in=(\d+) out=(\d+) irq=(\d+) physical=(\d+) c_exits=(\d+) host_irq=(\d+)',s)
assert len(rows)==1,rows
st=dict(zip(('enabled','fast_in','fast_out','fast_irq','physical','c_exits','host_irq'),map(int,rows[0])))
assert st['enabled']==1 and st['fast_in']>1000 and st['fast_out']>=1000
assert st['host_irq']>0
assert st['physical']==sum(st[k] for k in ('fast_in','fast_out','fast_irq','c_exits'))
r={'qemu_exit':0,'verifier_exit':0,'host_vmm_died_by_sigterm':True,
   'fresh_vm_passed':True,'final_counters':st,'console':s}
(p/'validation.json').write_text(json.dumps(r,ensure_ascii=False,indent=2)+'\n')
print('STAGE-E2-FAST: ACCEPT',json.dumps(st))
PY
