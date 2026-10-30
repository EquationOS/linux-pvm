/* SPDX-License-Identifier: GPL-2.0 */
#ifndef RISCV_PVM_SWITCH_H
#define RISCV_PVM_SWITCH_H
#define PVM_GATE_VA 0x3fffffe000
#define PVM_STATE_VA 0x3ffffff000
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
#ifndef __ASSEMBLY__
struct pvm_switch_state {
	unsigned long g[36], h[32];
	unsigned long host_satp, host_stvec, host_sscratch, host_status;
	unsigned long guest_satp, bridge_satp, return_pc, host_kva;
	unsigned long gfp[33], hfp[33];
	unsigned long host_scounteren, host_senvcfg;
};
extern char pvm_gate_start[], pvm_gate_end[];
void riscv_pvm_switch(struct pvm_switch_state *s, unsigned long bridge);
#define PVM_HSCNTEN 1136
#define PVM_HSENV    1144
#endif
#define PVM_HSCNTEN 1136
#define PVM_HSENV    1144
#endif
