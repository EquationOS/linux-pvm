/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PVM_MONITOR_TRAP_H
#define PVM_MONITOR_TRAP_H

/* Monitor-private integer trap frame. Linux entry.S builds its own pt_regs;
 * this array is never exposed as a synthetic PV event frame. */
#define TF_SIZE		(36*8)
#define TF_EPC_OFF	256	/* 32*8 */
#define TF_STATUS_OFF	264	/* 33*8 */
#define TF_CAUSE_OFF	272	/* 34*8 */
#define TF_TVAL_OFF	280	/* 35*8 */

#if !(defined(__ASSEMBLY__) || defined(__ASSEMBLER__))
extern unsigned long *trapframe;

void mtrap_c(void);
void mhost_enter_u(unsigned long epc, unsigned long sp, unsigned long tp,
		   unsigned long a0, unsigned long a1);
void mhost_hang(void);
#endif
#endif
