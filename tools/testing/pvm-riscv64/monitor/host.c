/* SPDX-License-Identifier: GPL-2.0 */
/* Experimental single-hart RV64 U-mode Linux monitor. Not a KVM backend. */
#include "trap.h"
/* Freestanding monitor imports constants, not Linux ABI type aliases. */
#define __ASSEMBLY__
#include <asm/pvm_para.h>
#undef __ASSEMBLY__
#define RAM_LO 0x80200000UL
#define RAM_HI 0x88000000UL
#define SH_LO 0x80100000UL
#define SH_HI 0x80200000UL
#define SIE 2UL
#define SPIE 0x20UL
#define SPP 0x100UL
#define SUM 0x40000UL
#define MXR 0x80000UL
#define R(n) trapframe[n]
unsigned long *trapframe;
static unsigned long scsr[0x100], gsatp, shnext, shroot;
static unsigned long exits, faults, rebuilds, user_entries, timer_irqs;
static int vs = 1, timer_pending, registered;
static unsigned long pvcs, kernel_tp;
static void puts(const char *s)
{
	while (*s)
		*(volatile char *)0x10000000 = *s++;
}
static void hex(unsigned long x)
{
	for (int n = 60; n >= 0; n -= 4)
		*(volatile char *)0x10000000 = "0123456789abcdef"[(x >> n) & 15];
}
static __attribute__((noreturn)) void stop(int ok)
{
	puts("\nPVM_HOST_STATS exits=");
	hex(exits);
	puts(" faults=");
	hex(faults);
	puts(" rebuilds=");
	hex(rebuilds);
	puts(" user_entries=");
	hex(user_entries);
	puts(" timer_irqs=");
	hex(timer_irqs);
	puts(" registered=");
	hex(registered);
	puts(" pvcs=");
	hex(pvcs);
	puts(" kernel_tp=");
	hex(kernel_tp);
	puts("\n");
	*(volatile unsigned *)0x100000 = ok ? 0x5555 : 0x13333;
	for (;;)
		asm volatile("wfi");
}
static __attribute__((noreturn)) void fatal(const char *s)
{
	puts("\nPVM_HOST_FAIL ");
	puts(s);
	puts(" pc=");
	hex(R(32));
	puts(" cause=");
	hex(R(34));
	puts(" tval=");
	hex(R(35));
	puts("\n");
	stop(0);
}
static int ram(unsigned long p, unsigned long n)
{
	return p >= RAM_LO && p < RAM_HI && n <= RAM_HI - p;
}
static int allowed(unsigned long p, unsigned long n)
{
	return ram(p, n) || (p >= 0x0c000000UL && p + n <= 0x10010000UL);
}
static unsigned long alloc(void)
{
	unsigned long p = shnext;
	shnext += 4096;
	if (shnext > SH_HI)
		fatal("shadow pool exhausted");
	for (int i = 0; i < 512; i++)
		((unsigned long *)p)[i] = 0;
	return p;
}
/* Filter every byte of superpages against owned RAM, splitting partial leaves. */
static unsigned long leaf(unsigned long pte, int lev)
{
	unsigned long size = 1UL << (12 + 9 * lev), pa = (pte >> 10 & 0xfffffffffffUL) << 12;
	if (pa & (size - 1))
		return 0;
	if (!vs && !(pte & 0x10))
		return 0;
	if (vs && (pte & 0x10) && !(scsr[0] & SUM))
		return 0;
	unsigned long bits = (pte & 0x3ff) | 0xd0;
	if (vs && (pte & 0x10)) {
		if ((scsr[0] & MXR) && (pte & 8))
			bits |= 2;
		bits &= ~8UL;
	}
	if (allowed(pa, size))
		return (pa >> 12 << 10) | bits;
	if (!lev)
		return 0;
	if ((pa >= RAM_HI || pa + size <= RAM_LO) &&
	    !(pa < 0x10010000UL && pa + size > 0x0c000000UL))
		return 0;
	unsigned long t = alloc(), step = size >> 9;
	for (int i = 0; i < 512; i++)
		((unsigned long *)t)[i] =
		    leaf(((pa + i * step) >> 12 << 10) | (pte & 0x3ff), lev - 1);
	return (t >> 12 << 10) | 1;
}
static unsigned long copy_table(unsigned long pa, int lev)
{
	if (!ram(pa, 4096))
		fatal("guest table outside RAM");
	unsigned long dst = alloc();
	for (int i = 0; i < 512; i++) {
		unsigned long p = ((unsigned long *)pa)[i];
		if (!(p & 1) || (p & 6) == 4)
			continue;
		if (p & 0xe)
			((unsigned long *)dst)[i] = leaf(p, lev);
		else if (lev)
			((unsigned long *)dst)[i] =
			    (copy_table((p >> 10 & 0xfffffffffffUL) << 12, lev - 1) >> 12 << 10) |
			    1;
	}
	return dst;
}
static void rebuild(void)
{
	rebuilds++;
	if (!gsatp) {
		asm volatile("csrw satp,zero; sfence.vma" ::: "memory");
		return;
	}
	shnext = SH_LO;
	shroot = copy_table((gsatp & 0xfffffffffffUL) << 12, 2);
	unsigned long s = (8UL << 60) | (shroot >> 12);
	asm volatile("csrw satp,%0; sfence.vma" ::"r"(s) : "memory");
}
static unsigned long translate(unsigned long va)
{
	if (!gsatp)
		return allowed(va, 2) ? va : 0;
	unsigned long t = (gsatp & 0xfffffffffffUL) << 12;
	for (int lev = 2; lev >= 0; lev--) {
		if (!ram(t, 4096))
			return 0;
		unsigned long p = ((unsigned long *)t)[va >> (12 + 9 * lev) & 511];
		if (!(p & 1))
			return 0;
		if (p & 0xe) {
			unsigned long sz = 1UL << (12 + 9 * lev), pa = (p >> 10 & 0xfffffffffffUL)
								       << 12;
			if (pa & (sz - 1))
				return 0;
			pa += va & (sz - 1);
			return allowed(pa, 2) ? pa : 0;
		}
		t = (p >> 10 & 0xfffffffffffUL) << 12;
	}
	return 0;
}
static void event(unsigned long cause, unsigned long val)
{
	unsigned long s = scsr[0];
	scsr[0x41] = R(32);
	scsr[0x42] = cause;
	scsr[0x43] = val;
	scsr[0] = (s & ~(SPP | SPIE | SIE)) | (vs ? SPP : 0) | ((s & SIE) ? SPIE : 0);
	if (!vs) {
		vs = 1;
		rebuild();
	}
	unsigned long vec = scsr[5];
	if (!(vec & ~3UL))
		fatal("event without stvec");
	R(32) = (vec & ~3UL) + ((vec & 1) && (cause >> 63) ? 4 * (cause & 63) : 0);
}
static unsigned long readcsr(unsigned long c)
{
	if (c == 0x180)
		return gsatp;
	if (c == 0xc00) {
		unsigned long v;
		asm volatile("rdcycle %0" : "=r"(v));
		return v;
	}
	if (c == 0xc01) {
		unsigned long v;
		asm volatile("rdtime %0" : "=r"(v));
		return v;
	}
	if (c == 0xc02) {
		unsigned long v;
		asm volatile("rdinstret %0" : "=r"(v));
		return v;
	}
	if (c == 0x100) {
		unsigned long s = scsr[0];
		if ((R(33) & 0x6000) == 0x6000)
			s |= 0x6000;
		return s | ((s & 0x6000) == 0x6000 ? 1UL << 63 : 0);
	}
	if (c >= 0x100 && c < 0x200)
		return scsr[c - 0x100];
	fatal("unsupported CSR read");
}
static void writecsr(unsigned long c, unsigned long v)
{
	if (c == 0x180) {
		if ((v >> 60) != 0 && (v >> 60) != 8)
			return;
		gsatp = v;
		rebuild();
		return;
	}
	if (c >= 0x100 && c < 0x200) {
		unsigned long old = scsr[c - 0x100];
		scsr[c - 0x100] = v;
		if (c == 0x100 && ((old ^ v) & (SUM | MXR)))
			rebuild();
		return;
	}
	fatal("unsupported CSR write");
}
static void sbi(void)
{
	unsigned long eid = R(17), fid = R(16), err = 0, val = 0;
	if (eid == PVM_HC_EID) {
		long ret = -38;
		switch (fid) {
		case PVM_HC_PROBE:
			ret = R(10) == PVM_ABI_VERSION ? PVM_ABI_VERSION : -22;
			break;
		case PVM_HC_GET_FEATURES:
#ifdef PVM_TEST_NO_CSR_FEATURE
			ret = 0;
#else
			ret = PVM_FEATURE_CSR_TRAPS;
#endif
			break;
		case PVM_HC_REGISTER_PVCS:
			if (registered)
				ret = -16;
			else if (R(10) != PVM_REGISTER_CSR_EVENTS || (R(11) & 4095) ||
				 !ram(R(11), 4096) || R(12) != R(4) || !ram(translate(R(12)), 8))
				ret = -22;
			else {
				pvcs = R(11);
				kernel_tp = R(12);
				registered = 1;
				ret = 0;
			}
			break;
		case PVM_HC_DEREGISTER:
			pvcs = kernel_tp = 0;
			registered = 0;
			ret = 0;
			break;
		case PVM_HC_LOAD_PGTBL:
			if (registered && !R(10) && ((R(11) >> 60) == 0 || (R(11) >> 60) == 8)) {
				gsatp = R(11);
				rebuild();
				ret = 0;
			} else
				ret = -22;
			break;
		case PVM_HC_TLB_FLUSH:
		case PVM_HC_TLB_FLUSH_CURRENT:
		case PVM_HC_TLB_INVLPG:
			if (registered) {
				rebuild();
				ret = 0;
			} else
				ret = -1;
			break;
		case PVM_HC_IRQ_WIN:
		case PVM_HC_IRQ_HALT:
			ret = registered ? 0 : -1;
			break;
		}
		R(10) = ret;
		R(11) = 0;
		R(32) += 4;
		return;
	}
	if (eid == 0x10) {
		switch (fid) {
		case 0:
			val = 0x02000000;
			break;
		case 1:
			val = 0x50564d;
			break;
		case 2:
			val = 1;
			break;
		case 3:
			val = R(10) == 0x10 || R(10) == 0x54494d45 || R(10) == 0x53525354;
			break;
		default:
			val = 0;
			break;
		}
	} else if (eid == 0x54494d45 && fid == 0) {
		*(volatile unsigned long *)0x2004000 = R(10);
		timer_pending = 0;
		scsr[0x44] &= ~0x20UL;
		asm volatile("csrs mie,%0" ::"r"(0x80UL));
	} else if (eid == 0x53525354 && fid == 0) {
		puts("PVM_HOST_GUEST_SHUTDOWN\n");
		stop(R(11) == 0);
	} else if (eid == 1) {
		*(volatile char *)0x10000000 = (char)R(10);
	} else
		err = -2UL;
	R(10) = err;
	R(11) = val;
	R(32) += 4;
}
void mtrap_c(void)
{
	exits++;
	if ((R(33) & 0x1800) != 0)
		fatal("nested M-mode trap");
	unsigned long c = R(34), pc = R(32);
	if (c == (1UL << 63 | 9)) {
		asm volatile("csrc mie,%0" ::"r"(0x200UL));
	} else if (c == (1UL << 63 | 7)) {
		asm volatile("csrc mie,%0" ::"r"(0x80UL));
		timer_pending = 1;
		scsr[0x44] |= 0x20;
	} else if (c == 2) {
		unsigned long pa = translate(pc), ins = 0;
		if (!pa)
			fatal("cannot decode instruction");
		ins = *(volatile unsigned short *)pa;
		if ((ins & 3) == 3) {
			unsigned long p2 = translate(pc + 2);
			if (!p2)
				fatal("split instruction");
			ins |= (unsigned long)*(volatile unsigned short *)p2 << 16;
		}
		if (!vs) {
			unsigned long csr = ins >> 20, op = ins >> 12 & 7;
			unsigned long src = ins >> 15 & 31, rd = ins >> 7 & 31;

			/* vDSO counter reads retain the guest's scounteren policy. */
			if ((ins & 0x7f) == 0x73 && (op == 2 || op == 3 || op == 6 || op == 7) &&
			    !src && csr >= 0xc00 && csr <= 0xc02 &&
			    (scsr[6] & (1UL << (csr - 0xc00)))) {
				if (rd)
					R(rd) = readcsr(csr);
				R(32) += 4;
				goto out;
			}
			event(2, ins);
			goto out;
		}
		if (ins == 0x10200073) {
			unsigned long s = scsr[0];
			vs = !!(s & SPP);
			scsr[0] = (s & ~(SIE | SPP)) | ((s & SPIE) ? SIE : 0) | SPIE;
			if (!vs)
				user_entries++;
			R(32) = scsr[0x41];
			rebuild();
		} else if ((ins & 0xfe007fffUL) == 0x12000073UL) {
			rebuild();
			R(32) += 4;
		} else if (ins == 0x10500073) {
			R(32) += 4;
		} else if ((ins & 0x7f) == 0x73 && ((ins >> 12) & 7)) {
			unsigned long op = ins >> 12 & 7, rs = ins >> 15 & 31, rd = ins >> 7 & 31,
				      csr = ins >> 20;
			if (csr == 0x180 && (op == 1 || op == 5) &&
			    ((op & 4 ? rs : R(rs)) >> 60) > 8) {
				if (rd)
					R(rd) = gsatp;
				R(32) += 4;
				goto out;
			}
			unsigned long old = readcsr(csr), v = (op & 4) ? rs : (rs ? R(rs) : 0),
				      nv = old;
			if ((op & 3) == 1)
				nv = v;
			else if ((op & 3) == 2)
				nv = old | v;
			else if ((op & 3) == 3)
				nv = old & ~v;
			if ((op & 3) == 1 || rs)
				writecsr(csr, nv);
			if (rd)
				R(rd) = old;
			R(32) += 4;
		} else {
			puts("unknown instruction ");
			hex(ins);
			fatal("unsupported instruction");
		}
	} else if (c == 8) {
		if (vs)
			sbi();
		else
			event(8, 0);
	} else if (c == 12 || c == 13 || c == 15) {
		faults++;
		unsigned long p = translate(c == 12 ? pc : R(35));
		static unsigned long lastpc, lastval;
		static int lastvs;
		if (p && (lastpc != pc || lastval != R(35) || lastvs != vs)) {
			lastpc = pc;
			lastval = R(35);
			lastvs = vs;
			rebuild();
		} else
			event(c, R(35));
	} else {
		event(c, R(35));
	}
out:
	if (timer_pending && (scsr[4] & 0x20) && (!vs || (scsr[0] & SIE))) {
		timer_irqs++;
		event(1UL << 63 | 5, 0);
	}
	unsigned long mip;
	asm volatile("csrr %0,mip" : "=r"(mip));
	if ((mip & 0x200) && (scsr[4] & 0x200) && (!vs || (scsr[0] & SIE)))
		event(1UL << 63 | 9, 0);
	if ((scsr[4] & 0x200) && (!vs || (scsr[0] & SIE)))
		asm volatile("csrs mie,%0" ::"r"(0x200UL));
	else
		asm volatile("csrc mie,%0" ::"r"(0x200UL));
	/* Never copy virtual SPP or any MPP/MPRV/MIE bits into real status. */
	R(33) = (scsr[0] & (0x6000 | MXR)) | (2UL << 32);
}
void host_main(void)
{
	/* Firmware hands a supervisor kernel user counter access enabled. */
	scsr[6] = 7;
	unsigned long fdt = 0;
	for (unsigned long p = RAM_HI - 8; p > 0x86000000UL; p -= 8)
		if (*(volatile unsigned *)p == 0xedfe0dd0U) {
			fdt = p;
			break;
		}
	puts("PVM_HOST_UMODE_LINUX hart=0 fdt=");
	hex(fdt);
	puts("\n");
	if (!fdt)
		for (;;)
			asm volatile("wfi");
	mhost_enter_u(RAM_LO, 0x80600000, 0, 0, fdt);
	for (;;)
		asm volatile("wfi");
}
