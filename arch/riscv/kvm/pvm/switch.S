/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>
#include <asm/asm.h>
#include <asm/csr.h>
#include "switch.h"
.option norelax
.option arch, +d
.text

/* Only t1 is clobbered; every call site has saved guest GPRs. */
.macro PVM_COUNT off
 ld t1,\off(a0)
 addi t1,t1,1
 sd t1,\off(a0)
.endm
.macro PVM_ROOT off
 ld t0,\off(a0)
 csrw satp,t0
 PVM_COUNT PVM_F_ROOT_WRITES
 sfence.vma
 PVM_COUNT PVM_F_SFENCES
.endm
SYM_FUNC_START(riscv_pvm_switch)
 sd ra,PVM_HREG+8(a0)
 sd sp,PVM_HREG+16(a0)
 sd gp,PVM_HREG+24(a0)
 sd tp,PVM_HREG+32(a0)
 sd t0,PVM_HREG+40(a0)
 sd t1,PVM_HREG+48(a0)
 sd t2,PVM_HREG+56(a0)
 sd s0,PVM_HREG+64(a0)
 sd s1,PVM_HREG+72(a0)
 sd a0,PVM_HREG+80(a0)
 sd a1,PVM_HREG+88(a0)
 sd a2,PVM_HREG+96(a0)
 sd a3,PVM_HREG+104(a0)
 sd a4,PVM_HREG+112(a0)
 sd a5,PVM_HREG+120(a0)
 sd a6,PVM_HREG+128(a0)
 sd a7,PVM_HREG+136(a0)
 sd s2,PVM_HREG+144(a0)
 sd s3,PVM_HREG+152(a0)
 sd s4,PVM_HREG+160(a0)
 sd s5,PVM_HREG+168(a0)
 sd s6,PVM_HREG+176(a0)
 sd s7,PVM_HREG+184(a0)
 sd s8,PVM_HREG+192(a0)
 sd s9,PVM_HREG+200(a0)
 sd s10,PVM_HREG+208(a0)
 sd s11,PVM_HREG+216(a0)
 sd t3,PVM_HREG+224(a0)
 sd t4,PVM_HREG+232(a0)
 sd t5,PVM_HREG+240(a0)
 sd t6,PVM_HREG+248(a0)
 csrr t0,satp
 sd t0,PVM_HSATP(a0)
 csrr t0,stvec
 sd t0,PVM_HSTVEC(a0)
 csrr t0,sscratch
 sd t0,PVM_HSCRATCH(a0)
 csrr t0,sstatus
 sd t0,PVM_HSTATUS(a0)
 csrr t0,scounteren
 sd t0,PVM_HSCNTEN(a0)
 csrr t0,senvcfg
 sd t0,PVM_HSENV(a0)
 sd a1,PVM_BRIDGE(a0)
 la t0,.Lhost_return
 sd t0,PVM_RETURN(a0)
 sd a0,PVM_HOST_KVA(a0)
 li t0,0x6000
 csrs sstatus,t0
 fsd f0,PVM_HFP+0(a0)
 fsd f1,PVM_HFP+8(a0)
 fsd f2,PVM_HFP+16(a0)
 fsd f3,PVM_HFP+24(a0)
 fsd f4,PVM_HFP+32(a0)
 fsd f5,PVM_HFP+40(a0)
 fsd f6,PVM_HFP+48(a0)
 fsd f7,PVM_HFP+56(a0)
 fsd f8,PVM_HFP+64(a0)
 fsd f9,PVM_HFP+72(a0)
 fsd f10,PVM_HFP+80(a0)
 fsd f11,PVM_HFP+88(a0)
 fsd f12,PVM_HFP+96(a0)
 fsd f13,PVM_HFP+104(a0)
 fsd f14,PVM_HFP+112(a0)
 fsd f15,PVM_HFP+120(a0)
 fsd f16,PVM_HFP+128(a0)
 fsd f17,PVM_HFP+136(a0)
 fsd f18,PVM_HFP+144(a0)
 fsd f19,PVM_HFP+152(a0)
 fsd f20,PVM_HFP+160(a0)
 fsd f21,PVM_HFP+168(a0)
 fsd f22,PVM_HFP+176(a0)
 fsd f23,PVM_HFP+184(a0)
 fsd f24,PVM_HFP+192(a0)
 fsd f25,PVM_HFP+200(a0)
 fsd f26,PVM_HFP+208(a0)
 fsd f27,PVM_HFP+216(a0)
 fsd f28,PVM_HFP+224(a0)
 fsd f29,PVM_HFP+232(a0)
 fsd f30,PVM_HFP+240(a0)
 fsd f31,PVM_HFP+248(a0)
 frcsr t0
 sd t0,PVM_HFP+256(a0)
 fld f0,PVM_GFP+0(a0)
 fld f1,PVM_GFP+8(a0)
 fld f2,PVM_GFP+16(a0)
 fld f3,PVM_GFP+24(a0)
 fld f4,PVM_GFP+32(a0)
 fld f5,PVM_GFP+40(a0)
 fld f6,PVM_GFP+48(a0)
 fld f7,PVM_GFP+56(a0)
 fld f8,PVM_GFP+64(a0)
 fld f9,PVM_GFP+72(a0)
 fld f10,PVM_GFP+80(a0)
 fld f11,PVM_GFP+88(a0)
 fld f12,PVM_GFP+96(a0)
 fld f13,PVM_GFP+104(a0)
 fld f14,PVM_GFP+112(a0)
 fld f15,PVM_GFP+120(a0)
 fld f16,PVM_GFP+128(a0)
 fld f17,PVM_GFP+136(a0)
 fld f18,PVM_GFP+144(a0)
 fld f19,PVM_GFP+152(a0)
 fld f20,PVM_GFP+160(a0)
 fld f21,PVM_GFP+168(a0)
 fld f22,PVM_GFP+176(a0)
 fld f23,PVM_GFP+184(a0)
 fld f24,PVM_GFP+192(a0)
 fld f25,PVM_GFP+200(a0)
 fld f26,PVM_GFP+208(a0)
 fld f27,PVM_GFP+216(a0)
 fld f28,PVM_GFP+224(a0)
 fld f29,PVM_GFP+232(a0)
 fld f30,PVM_GFP+240(a0)
 fld f31,PVM_GFP+248(a0)
 ld t0,PVM_GFP+256(a0)
 fscsr t0
 csrw satp,a1
 PVM_COUNT PVM_F_ROOT_WRITES
 sfence.vma
 PVM_COUNT PVM_F_SFENCES
 li t0,PVM_GATE_VA
 jr t0
.Lhost_return:
 ld a0,PVM_HOST_KVA(a0)
 ld t0,PVM_HSATP(a0)
 csrw satp,t0
 PVM_COUNT PVM_F_ROOT_WRITES
 sfence.vma
 PVM_COUNT PVM_F_SFENCES
 ld t0,PVM_HSTVEC(a0)
 csrw stvec,t0
 ld t0,PVM_HSCRATCH(a0)
 csrw sscratch,t0
 ld t0,PVM_HSTATUS(a0)
 csrw sstatus,t0
 ld ra,PVM_HREG+8(a0)
 ld sp,PVM_HREG+16(a0)
 ld gp,PVM_HREG+24(a0)
 ld tp,PVM_HREG+32(a0)
 ld s0,PVM_HREG+64(a0)
 ld s1,PVM_HREG+72(a0)
 ld s2,PVM_HREG+144(a0)
 ld s3,PVM_HREG+152(a0)
 ld s4,PVM_HREG+160(a0)
 ld s5,PVM_HREG+168(a0)
 ld s6,PVM_HREG+176(a0)
 ld s7,PVM_HREG+184(a0)
 ld s8,PVM_HREG+192(a0)
 ld s9,PVM_HREG+200(a0)
 ld s10,PVM_HREG+208(a0)
 ld s11,PVM_HREG+216(a0)
 ret
SYM_FUNC_END(riscv_pvm_switch)

.balign 4096
.global pvm_gate_start,pvm_gate_end
pvm_gate_start:
 li a0,PVM_STATE_VA
 /* Isolate host counter/envcfg state: guests must trap into the
  * emulator's virtual permission checks instead of inheriting the
  * host VMM's scounteren/senvcfg.  Runs with host IRQs/preempt off. */
 csrw scounteren,zero
 csrw senvcfg,zero
 /* The gate executes from its relocated copy at PVM_GATE_VA, so a
  * pc-relative la resolves to the runtime address directly. */
 la t0,.Lgate_trap
 csrw stvec,t0
 csrw sscratch,a0
 ld t0,PVM_GSATP(a0)
 csrw satp,t0
 PVM_COUNT PVM_F_ROOT_WRITES
 sfence.vma
 PVM_COUNT PVM_F_SFENCES
.Lgate_restore:
 csrw sscratch,a0
 ld t0,256(a0)
 csrw sepc,t0
 ld t0,264(a0)
 csrw sstatus,t0
 ld ra,8(a0)
 ld sp,16(a0)
 ld gp,24(a0)
 ld tp,32(a0)
 ld t0,40(a0)
 ld t1,48(a0)
 ld t2,56(a0)
 ld s0,64(a0)
 ld s1,72(a0)
 ld a1,88(a0)
 ld a2,96(a0)
 ld a3,104(a0)
 ld a4,112(a0)
 ld a5,120(a0)
 ld a6,128(a0)
 ld a7,136(a0)
 ld s2,144(a0)
 ld s3,152(a0)
 ld s4,160(a0)
 ld s5,168(a0)
 ld s6,176(a0)
 ld s7,184(a0)
 ld s8,192(a0)
 ld s9,200(a0)
 ld s10,208(a0)
 ld s11,216(a0)
 ld t3,224(a0)
 ld t4,232(a0)
 ld t5,240(a0)
 ld t6,248(a0)
 ld a0,80(a0)
 sret
.balign 4
.Lgate_trap:
 csrrw a0,sscratch,a0
 sd ra,8(a0)
 sd sp,16(a0)
 sd gp,24(a0)
 sd tp,32(a0)
 sd t0,40(a0)
 sd t1,48(a0)
 sd t2,56(a0)
 sd s0,64(a0)
 sd s1,72(a0)
 sd a1,88(a0)
 sd a2,96(a0)
 sd a3,104(a0)
 sd a4,112(a0)
 sd a5,120(a0)
 sd a6,128(a0)
 sd a7,136(a0)
 sd s2,144(a0)
 sd s3,152(a0)
 sd s4,160(a0)
 sd s5,168(a0)
 sd s6,176(a0)
 sd s7,184(a0)
 sd s8,192(a0)
 sd s9,200(a0)
 sd s10,208(a0)
 sd s11,216(a0)
 sd t3,224(a0)
 sd t4,232(a0)
 sd t5,240(a0)
 sd t6,248(a0)
 csrr t0,sscratch
 sd t0,80(a0)
 csrr t0,sepc
 sd t0,256(a0)
 csrr t0,sstatus
 sd t0,264(a0)
 csrr t0,scause
 sd t0,272(a0)
 csrr t0,stval
 sd t0,280(a0)
 /* Every physical trap is counted, including the C path. */
 PVM_COUNT PVM_F_PHYS_TRAPS
 ld t0,264(a0)
 andi t0,t0,SR_SPP
 bnez t0,.Lgate_slow
 ld t0,PVM_F_ENABLED(a0)
 beqz t0,.Lgate_slow
 ld t0,272(a0)
 li t1,8
 bne t0,t1,.Lgate_slow /* all real host IRQs leave the gate */
 ld t0,PVM_F_VIRT_SUP(a0)
 beqz t0,.Lpv_syscall
 ld t0,136(a0)
 li t1,0x70766d
 bne t0,t1,.Lgate_slow
 ld t0,128(a0)
 li t1,0x1708820e
 beq t0,t1,.Lpv_return
 li t1,0x17088210
 beq t0,t1,.Lpv_irq_read
 li t1,0x17088211
 beq t0,t1,.Lpv_irq_restore
 li t1,0x17088212
 beq t0,t1,.Lpv_irq_disable
 j .Lgate_slow

.Lpv_syscall:
 ld t0,PVM_F_CACHE_VALID(a0)
 beqz t0,.Lpv_fallback
 ld t0,PVM_F_VIRT_STATUS(a0)
 li t1,(SR_SUM|0x80000)
 and t1,t0,t1
 bnez t1,.Lpv_fallback
 /* Host KVA is meaningful only after entering the bridge root. */
 PVM_ROOT PVM_BRIDGE
 ld t2,PVM_F_FRAME_KVA(a0)
 ld t3,256(a0)
 sd t3,0(t2)
 addi t4,a0,8
 addi t5,t2,8
 li t6,31
.Lpv_copy_in:
 ld t3,0(t4)
 sd t3,0(t5)
 addi t4,t4,8
 addi t5,t5,8
 addi t6,t6,-1
 bnez t6,.Lpv_copy_in
 /* Build the USER post-trap status before disabling kernel FP.
  * Fast admission excludes SUM/MXR; UXL is fixed to RV64. */
 li t3,0x200000000
 ld t4,PVM_F_VIRT_STATUS(a0)
 andi t4,t4,SR_SIE
 beqz t4,.Lpv_no_spie
 ori t3,t3,SR_SPIE
.Lpv_no_spie:
 ld t4,264(a0)
 li t5,SR_FS
 and t4,t4,t5
 or t3,t3,t4
 li t5,SR_FS_DIRTY
 bne t4,t5,.Lpv_no_sd
 li t5,SR_SD
 or t3,t3,t5
.Lpv_no_sd:
 sd t3,256(t2)
 sd zero,264(t2)
 li t4,8
 sd t4,272(t2)
 ld t4,80(a0)
 sd t4,280(t2)
 andi t3,t3,SR_SPIE
 sd t3,PVM_F_VIRT_STATUS(a0) /* kernel FS/SUM Off, SIE=0 */
 li t3,1
 sd t3,PVM_F_VIRT_SUP(a0)
 ld t3,256(a0)
 sd t3,PVM_F_VIRT_SEPC(a0)
 li t3,8
 sd t3,PVM_F_VIRT_SCAUSE(a0)
 sd zero,PVM_F_VIRT_STVAL(a0)
 sd zero,PVM_F_VIRT_SSCRATCH(a0)
 /* C entry starts BELOW the frame, just as in the E1 C path. */
 ld t3,PVM_F_FRAME_GVA(a0)
 sd t3,16(a0)
 sd t3,80(a0)
 ld t3,PVM_F_KERNEL_GP(a0)
 sd t3,24(a0)
 ld t3,PVM_F_KERNEL_TP(a0)
 sd t3,32(a0)
 ld t3,PVM_F_ENTRY_GVA(a0)
 sd t3,256(a0)
 li t3,0x200000020 /* real UXL64, SPIE=1, FS/SIE/SPP=0 */
 sd t3,264(a0)
 PVM_COUNT PVM_F_FAST_IN
 PVM_ROOT PVM_F_KERNEL_ROOT
 j .Lgate_restore

.Lpv_return:
 ld t0,PVM_F_CACHE_VALID(a0)
 beqz t0,.Lpv_fallback
 ld t0,80(a0)
 ld t1,PVM_F_FRAME_GVA(a0)
 bne t0,t1,.Lpv_fallback
 ld t0,32(a0)
 ld t1,PVM_F_KERNEL_TP(a0)
 bne t0,t1,.Lpv_fallback
 ld t0,PVM_F_VIRT_STATUS(a0)
 li t1,(SR_SUM|0x80000)
 and t0,t0,t1
 bnez t0,.Lpv_fallback
 PVM_ROOT PVM_BRIDGE
 ld t2,PVM_F_FRAME_KVA(a0)
 ld t3,256(t2)
 li t4,(SR_SPP|SR_SUM|0x80000)
 and t4,t3,t4
 bnez t4,.Lpv_fallback
 /* Copy all GPRs while the host KVA is mapped.  The common restore
  * uses only the private state page under the final user root. */
 addi t4,t2,8
 addi t5,a0,8
 li t6,31
.Lpv_copy_out:
 ld t0,0(t4)
 sd t0,0(t5)
 addi t4,t4,8
 addi t5,t5,8
 addi t6,t6,-1
 bnez t6,.Lpv_copy_out
 ld t0,0(t2)
 andi t0,t0,-2
 sd t0,256(a0)
 sd t0,PVM_F_VIRT_SEPC(a0)
 /* Virtual sret: SIE=frame.SPIE, SPIE=1, SPP=0; only the
  * supported FS bits survive this conservative fast admission. */
 li t4,SR_FS
 and t4,t3,t4
 ori t5,t4,SR_SPIE
 andi t6,t3,SR_SPIE
 beqz t6,.Lpv_return_irq_masked
 ori t5,t5,SR_SIE
.Lpv_return_irq_masked:
 sd t5,PVM_F_VIRT_STATUS(a0)
 sd zero,PVM_F_VIRT_SUP(a0)
 ld t5,PVM_F_KERNEL_TP(a0)
 sd t5,PVM_F_VIRT_SSCRATCH(a0)
 li t5,0x200000020
 or t4,t4,t5
 sd t4,264(a0) /* real FS from frame, always host IRQ-enabled U */
 PVM_COUNT PVM_F_FAST_OUT
 PVM_ROOT PVM_F_USER_ROOT
 j .Lgate_restore

.Lpv_irq_read:
 ld t0,PVM_F_VIRT_STATUS(a0)
 andi t1,t0,SR_SIE
 sd t1,80(a0)
 j .Lpv_irq_done
.Lpv_irq_disable:
 ld t0,PVM_F_VIRT_STATUS(a0)
 andi t1,t0,SR_SIE
 sd t1,80(a0)
 andi t0,t0,-3
 sd t0,PVM_F_VIRT_STATUS(a0)
 j .Lpv_irq_done
.Lpv_irq_restore:
 ld t0,80(a0)
 andi t0,t0,SR_SIE
 beqz t0,.Lpv_irq_restore_off
 ld t1,PVM_F_PENDING_IRQ(a0)
 bnez t1,.Lpv_fallback
 ld t1,PVM_F_VIRT_STATUS(a0)
 ori t1,t1,SR_SIE
 j .Lpv_irq_restore_store
.Lpv_irq_restore_off:
 ld t1,PVM_F_VIRT_STATUS(a0)
 andi t1,t1,-3
.Lpv_irq_restore_store:
 sd t1,PVM_F_VIRT_STATUS(a0)
 sd zero,80(a0)
.Lpv_irq_done:
 sd zero,88(a0) /* hypercall second return value */
 ld t0,256(a0)
 addi t0,t0,4
 sd t0,256(a0)
 ld t0,264(a0)
 li t1,(SR_FS|0x80000)
 and t0,t0,t1
 li t1,0x200000020
 or t0,t0,t1
 sd t0,264(a0)
 PVM_COUNT PVM_F_FAST_IRQ
 j .Lgate_restore

.Lpv_fallback:
 /* A candidate misses the C-validated cache or needs full checking.
  * This is a subset of C exits, never an extra physical trap. */
 PVM_COUNT PVM_F_FALLBACK
.Lgate_slow:
 li t0,0x6000
 csrs sstatus,t0
 fsd f0,PVM_GFP+0(a0)
 fsd f1,PVM_GFP+8(a0)
 fsd f2,PVM_GFP+16(a0)
 fsd f3,PVM_GFP+24(a0)
 fsd f4,PVM_GFP+32(a0)
 fsd f5,PVM_GFP+40(a0)
 fsd f6,PVM_GFP+48(a0)
 fsd f7,PVM_GFP+56(a0)
 fsd f8,PVM_GFP+64(a0)
 fsd f9,PVM_GFP+72(a0)
 fsd f10,PVM_GFP+80(a0)
 fsd f11,PVM_GFP+88(a0)
 fsd f12,PVM_GFP+96(a0)
 fsd f13,PVM_GFP+104(a0)
 fsd f14,PVM_GFP+112(a0)
 fsd f15,PVM_GFP+120(a0)
 fsd f16,PVM_GFP+128(a0)
 fsd f17,PVM_GFP+136(a0)
 fsd f18,PVM_GFP+144(a0)
 fsd f19,PVM_GFP+152(a0)
 fsd f20,PVM_GFP+160(a0)
 fsd f21,PVM_GFP+168(a0)
 fsd f22,PVM_GFP+176(a0)
 fsd f23,PVM_GFP+184(a0)
 fsd f24,PVM_GFP+192(a0)
 fsd f25,PVM_GFP+200(a0)
 fsd f26,PVM_GFP+208(a0)
 fsd f27,PVM_GFP+216(a0)
 fsd f28,PVM_GFP+224(a0)
 fsd f29,PVM_GFP+232(a0)
 fsd f30,PVM_GFP+240(a0)
 fsd f31,PVM_GFP+248(a0)
 frcsr t0
 sd t0,PVM_GFP+256(a0)
 fld f0,PVM_HFP+0(a0)
 fld f1,PVM_HFP+8(a0)
 fld f2,PVM_HFP+16(a0)
 fld f3,PVM_HFP+24(a0)
 fld f4,PVM_HFP+32(a0)
 fld f5,PVM_HFP+40(a0)
 fld f6,PVM_HFP+48(a0)
 fld f7,PVM_HFP+56(a0)
 fld f8,PVM_HFP+64(a0)
 fld f9,PVM_HFP+72(a0)
 fld f10,PVM_HFP+80(a0)
 fld f11,PVM_HFP+88(a0)
 fld f12,PVM_HFP+96(a0)
 fld f13,PVM_HFP+104(a0)
 fld f14,PVM_HFP+112(a0)
 fld f15,PVM_HFP+120(a0)
 fld f16,PVM_HFP+128(a0)
 fld f17,PVM_HFP+136(a0)
 fld f18,PVM_HFP+144(a0)
 fld f19,PVM_HFP+152(a0)
 fld f20,PVM_HFP+160(a0)
 fld f21,PVM_HFP+168(a0)
 fld f22,PVM_HFP+176(a0)
 fld f23,PVM_HFP+184(a0)
 fld f24,PVM_HFP+192(a0)
 fld f25,PVM_HFP+200(a0)
 fld f26,PVM_HFP+208(a0)
 fld f27,PVM_HFP+216(a0)
 fld f28,PVM_HFP+224(a0)
 fld f29,PVM_HFP+232(a0)
 fld f30,PVM_HFP+240(a0)
 fld f31,PVM_HFP+248(a0)
 ld t0,PVM_HFP+256(a0)
 fscsr t0
 ld t0,PVM_BRIDGE(a0)
 csrw satp,t0
 PVM_COUNT PVM_F_ROOT_WRITES
 sfence.vma
 PVM_COUNT PVM_F_SFENCES
 /* restore host scounteren/senvcfg before re-enabling IRQ/preempt */
 ld t2,PVM_HSCNTEN(a0)
 csrw scounteren,t2
 ld t2,PVM_HSENV(a0)
 csrw senvcfg,t2
 ld t0,PVM_RETURN(a0)
 jr t0
pvm_gate_end:
