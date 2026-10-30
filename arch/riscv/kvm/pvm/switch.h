/* SPDX-License-Identifier: GPL-2.0 */
#ifndef RISCV_PVM_SWITCH_H
#define RISCV_PVM_SWITCH_H

#define PVM_GATE_VA 0x3fffffe000
#define PVM_STATE_VA 0x3ffffff000

/* Existing host save/restore area (E1 layout, unchanged) */
#define PVM_HREG 288
#define PVM_HSATP 544
#define PVM_HSTVEC 552
#define PVM_HSCRATCH 560
#define PVM_HSTATUS 568
#define PVM_GSATP 576
#define PVM_BRIDGE 584
#define PVM_RETURN 592
#define PVM_HOST_KVA 600
#define PVM_GFP 608
#define PVM_HFP 872

/* E2: fast-gate fields appended to the private state page, after
 * host_scounteren/host_senvcfg (1152).  All 8-byte aligned. */
#define PVM_FST_BASE 1152

/* virtual CPU state mirror (C <-> asm synchronized) */
#define PVM_F_VIRT_SUP 1152 /* u64: 1=supervisor,0=user */
#define PVM_F_VIRT_STATUS 1160 /* virtual sstatus (SIE/SPIE/SPP/FS/MXR/SUM) */
#define PVM_F_VIRT_SEPC 1168
#define PVM_F_VIRT_SSCRATCH 1176
#define PVM_F_VIRT_SCAUSE 1184
#define PVM_F_VIRT_STVAL 1192

/* fast-path cache (C prepared, asm consumed; guest-invisible) */
#define PVM_F_CACHE_VALID 1200 /* u64: 1=cache usable */
#define PVM_F_KERNEL_ROOT 1208 /* physical satp for kernel shadow */
#define PVM_F_USER_ROOT 1216 /* physical satp for user shadow */
#define PVM_F_KERNEL_TP 1224
#define PVM_F_KERNEL_GP 1232
#define PVM_F_FRAME_GVA 1240 /* guest VA of the 288B pt_regs frame */
#define PVM_F_ENTRY_GVA 1248
#define PVM_F_FRAME_KVA 1256 /* host KVA of the frame backing (bridge root) */

/* asm-real counters (64-bit, monotonic) */
#define PVM_F_PHYS_TRAPS 1264
#define PVM_F_ROOT_WRITES 1272
#define PVM_F_SFENCES 1280
#define PVM_F_FAST_IN 1288
#define PVM_F_FAST_OUT 1296
#define PVM_F_FAST_IRQ 1304
#define PVM_F_FALLBACK 1312
#define PVM_F_PENDING_IRQ 1320
#define PVM_F_ENABLED 1328

/* total state page usage: 1336 bytes < 4096 */

#ifndef __ASSEMBLY__
struct pvm_switch_state {
	unsigned long g[36], h[32];
	unsigned long host_satp, host_stvec, host_sscratch, host_status;
	unsigned long guest_satp, bridge_satp, return_pc, host_kva;
	unsigned long gfp[33], hfp[33];
	unsigned long host_scounteren, host_senvcfg;
	/* E2 fast-gate mirror (offsets PVM_F_* above) */
	unsigned long fast_virt_sup, fast_virt_status, fast_virt_sepc;
	unsigned long fast_virt_sscratch, fast_virt_scause, fast_virt_stval;
	unsigned long fast_cache_valid, fast_kernel_root, fast_user_root;
	unsigned long fast_kernel_tp, fast_kernel_gp, fast_frame_gva;
	unsigned long fast_entry_gva, fast_frame_kva;
	unsigned long fast_phys_traps, fast_root_writes, fast_sfences;
	unsigned long fast_in, fast_out, fast_irq, fast_fallback;
	unsigned long fast_pending_irq, fast_enabled;
};

extern char pvm_gate_start[], pvm_gate_end[];
void riscv_pvm_switch(struct pvm_switch_state *s, unsigned long bridge);

/* compile-time offset checks */
#define PVM_CHECK_OFFSET(f, off) \
	_Static_assert(offsetof(struct pvm_switch_state, f) == (off), \
		       #f " offset mismatch")

PVM_CHECK_OFFSET(host_scounteren, 1136);
PVM_CHECK_OFFSET(host_senvcfg, 1144);
PVM_CHECK_OFFSET(fast_virt_sup, PVM_F_VIRT_SUP);
PVM_CHECK_OFFSET(fast_virt_status, PVM_F_VIRT_STATUS);
PVM_CHECK_OFFSET(fast_cache_valid, PVM_F_CACHE_VALID);
PVM_CHECK_OFFSET(fast_frame_kva, PVM_F_FRAME_KVA);
PVM_CHECK_OFFSET(fast_phys_traps, PVM_F_PHYS_TRAPS);
PVM_CHECK_OFFSET(fast_fallback, PVM_F_FALLBACK);
PVM_CHECK_OFFSET(h, PVM_HREG);
PVM_CHECK_OFFSET(host_satp, PVM_HSATP);
PVM_CHECK_OFFSET(host_stvec, PVM_HSTVEC);
PVM_CHECK_OFFSET(host_sscratch, PVM_HSCRATCH);
PVM_CHECK_OFFSET(host_status, PVM_HSTATUS);
PVM_CHECK_OFFSET(guest_satp, PVM_GSATP);
PVM_CHECK_OFFSET(bridge_satp, PVM_BRIDGE);
PVM_CHECK_OFFSET(return_pc, PVM_RETURN);
PVM_CHECK_OFFSET(host_kva, PVM_HOST_KVA);
PVM_CHECK_OFFSET(gfp, PVM_GFP);
PVM_CHECK_OFFSET(hfp, PVM_HFP);
PVM_CHECK_OFFSET(fast_virt_sepc, PVM_F_VIRT_SEPC);
PVM_CHECK_OFFSET(fast_virt_sscratch, PVM_F_VIRT_SSCRATCH);
PVM_CHECK_OFFSET(fast_virt_scause, PVM_F_VIRT_SCAUSE);
PVM_CHECK_OFFSET(fast_virt_stval, PVM_F_VIRT_STVAL);
PVM_CHECK_OFFSET(fast_kernel_root, PVM_F_KERNEL_ROOT);
PVM_CHECK_OFFSET(fast_user_root, PVM_F_USER_ROOT);
PVM_CHECK_OFFSET(fast_kernel_tp, PVM_F_KERNEL_TP);
PVM_CHECK_OFFSET(fast_kernel_gp, PVM_F_KERNEL_GP);
PVM_CHECK_OFFSET(fast_frame_gva, PVM_F_FRAME_GVA);
PVM_CHECK_OFFSET(fast_entry_gva, PVM_F_ENTRY_GVA);
PVM_CHECK_OFFSET(fast_root_writes, PVM_F_ROOT_WRITES);
PVM_CHECK_OFFSET(fast_sfences, PVM_F_SFENCES);
PVM_CHECK_OFFSET(fast_in, PVM_F_FAST_IN);
PVM_CHECK_OFFSET(fast_out, PVM_F_FAST_OUT);
PVM_CHECK_OFFSET(fast_irq, PVM_F_FAST_IRQ);
PVM_CHECK_OFFSET(fast_pending_irq, PVM_F_PENDING_IRQ);
PVM_CHECK_OFFSET(fast_enabled, PVM_F_ENABLED);
#endif /* !__ASSEMBLY__ */

/* keep the old names for compatibility */
#define PVM_HSCNTEN 1136
#define PVM_HSENV 1144

#endif
