// SPDX-License-Identifier: GPL-2.0
#include "pvm.h"
#include <asm/cacheflush.h>
#include <asm/pgtable.h>

static unsigned long *root_alloc(struct pvm_root *r)
{
	unsigned long *p;
	if (r->used == PVM_POOL_PAGES)
		return NULL;
	p = r->tables[r->used++];
	memset(p, 0, PAGE_SIZE);
	return p;
}
static int map_page(struct pvm_root *r, unsigned long va, unsigned long pa,
		    unsigned long flags)
{
	unsigned long *t = r->root;
	int lev;
	for (lev = 2; lev > 0; lev--) {
		unsigned long *e = &t[(va >> (12 + 9 * lev)) & 511], *n;
		if (!(*e & 1)) {
			n = root_alloc(r);
			if (!n)
				return -ENOMEM;
			*e = (virt_to_phys(n) >> 12 << 10) | 1;
		}
		t = phys_to_virt((*e >> 10) << 12);
	}
	t[(va >> 12) & 511] = (pa >> 12 << 10) | flags;
	return 0;
}
static void reset(struct riscv_pvm_vcpu *v, struct pvm_root *r)
{
	r->used = 0;
	r->root = root_alloc(r);
	map_page(r, PVM_GATE_VA, virt_to_phys(v->gate),
		 0xcb); /* V,R,X,A,D; U=0 */
	map_page(r, PVM_STATE_VA, virt_to_phys(v->s),
		 0xc7); /* V,R,W,A,D; U=0 */
}
void pvm_reset_roots(struct riscv_pvm_vcpu *v)
{
	reset(v, &v->kernel);
	reset(v, &v->user);
}
static int init_root(struct pvm_root *r)
{
	unsigned int i;
	r->tables = kcalloc(PVM_POOL_PAGES, sizeof(*r->tables), GFP_KERNEL);
	if (!r->tables)
		return -ENOMEM;
	for (i = 0; i < PVM_POOL_PAGES; i++) {
		r->tables[i] = (void *)get_zeroed_page(GFP_KERNEL);
		if (!r->tables[i])
			return -ENOMEM;
	}
	return 0;
}
static void free_root(struct pvm_root *r)
{
	unsigned int i;
	if (!r->tables)
		return;
	for (i = 0; i < PVM_POOL_PAGES; i++)
		if (r->tables[i])
			free_page((unsigned long)r->tables[i]);
	kfree(r->tables);
	r->tables = NULL;
}
int pvm_mmu_init(struct riscv_pvm_vcpu *v)
{
	int err;
	if (pgtable_l4_enabled || pgtable_l5_enabled)
		return -EOPNOTSUPP;
	if (pvm_gate_end - pvm_gate_start > PAGE_SIZE)
		return -E2BIG;
	v->s = (void *)get_zeroed_page(GFP_KERNEL);
	v->gate = (void *)get_zeroed_page(GFP_KERNEL);
	if (!v->s || !v->gate)
		return -ENOMEM;
	memcpy(v->gate, pvm_gate_start, pvm_gate_end - pvm_gate_start);
	flush_icache_range((unsigned long)v->gate,
			   (unsigned long)v->gate + PAGE_SIZE);
	err = init_root(&v->bridge);
	if (err)
		return err;
	err = init_root(&v->kernel);
	if (err)
		return err;
	err = init_root(&v->user);
	if (err)
		return err;
	reset(v, &v->bridge);
	memcpy(v->bridge.root + 256, (unsigned long *)init_mm.pgd + 256,
	       256 * sizeof(unsigned long));
	pvm_reset_roots(v);
	return 0;
}
void pvm_mmu_destroy(struct riscv_pvm_vcpu *v)
{
	free_root(&v->bridge);
	free_root(&v->kernel);
	free_root(&v->user);
	if (v->gate)
		free_page((unsigned long)v->gate);
	if (v->s)
		free_page((unsigned long)v->s);
}
unsigned long pvm_real_satp(struct riscv_pvm_vcpu *v)
{
	return (8UL << 60) |
	       (virt_to_phys(v->supervisor ? v->kernel.root : v->user.root) >>
		12);
}
int pvm_bind_memory(struct kvm_vcpu *vcpu)
{
	struct riscv_pvm_vm *p = vcpu->kvm->arch.pvm;
	struct kvm_memory_slot *slot;
	unsigned long i;
	if (p->bound)
		return atomic_read(&p->invalid) ? -EFAULT : 0;
	slot = gfn_to_memslot(vcpu->kvm, PVM_RAM_BASE >> 12);
	if (!slot || slot->base_gfn != (PVM_RAM_BASE >> 12) ||
	    slot->npages != ((PVM_RAM_END - PVM_RAM_BASE) >> 12))
		return -EINVAL;
	p->nr_pages = slot->npages;
	p->pages = kvcalloc(p->nr_pages, sizeof(*p->pages),
			    GFP_KERNEL | __GFP_NOWARN);
	if (!p->pages) {
		p->nr_pages = 0;
		return -ENOMEM;
	}
	for (i = 0; i < p->nr_pages; i++) {
		kvm_pfn_t pfn = gfn_to_pfn(vcpu->kvm, (PVM_RAM_BASE >> 12) + i);
		if (is_error_noslot_pfn(pfn)) {
			/* unwind: drop pins taken so far and the array; p->bound stays
    * false so a later KVM_RUN may retry the bind cleanly. */
			while (i--)
				put_page(p->pages[i]);
			kvfree(p->pages);
			p->pages = NULL;
			p->nr_pages = 0;
			return -EFAULT;
		}
		p->pages[i] = pfn_to_page(pfn);
	}
	p->bound = true;
	return 0;
}
void *pvm_gpa(struct riscv_pvm_vcpu *v, unsigned long gpa, unsigned long size)
{
	struct riscv_pvm_vm *p = v->vcpu->kvm->arch.pvm;
	if (gpa < PVM_RAM_BASE || gpa >= PVM_RAM_END ||
	    size > PAGE_SIZE - offset_in_page(gpa) ||
	    size > PVM_RAM_END - gpa || !p->bound)
		return NULL;
	return page_address(p->pages[(gpa - PVM_RAM_BASE) >> 12]) +
	       offset_in_page(gpa);
}
static unsigned long walk(struct riscv_pvm_vcpu *v, unsigned long va,
			  unsigned long *pte, unsigned long **slot)
{
	unsigned long t = (v->satp & 0xfffffffffffUL) << 12;
	int lev;
	*slot = NULL;
	if (!v->satp) {
		/* Bare mode: no architectural PTE exists, so we synthesize
		 * one for the shadow-fill logic.  Architecturally bare
		 * mode has no U-bit separation, but our virtual identity
		 * does: USER gets U=1 (0xdf) so page_fault accepts the
		 * fill, KERNEL stays U=0 (0xcf).  The GPA itself is
		 * still strictly bounded by pvm_gpa() as everywhere. */
		*pte = v->supervisor ? 0xcf : 0xdf;
		return pvm_gpa(v, va, 1) ? va : 0;
	}
	if ((long)va >> 38 != 0 && (long)va >> 38 != -1)
		return 0;
	for (lev = 2; lev >= 0; lev--) {
		unsigned long *p = pvm_gpa(v, t, PAGE_SIZE), e, sz, pa;
		if (!p)
			return 0;
		p += va >> (12 + 9 * lev) & 511;
		e = READ_ONCE(*p);
		if (!(e & 1) || (e & 6) == 4 || (e >> 54))
			return 0;
		pa = (e >> 10 & 0xfffffffffffUL) << 12;
		if (e & 0xe) {
			sz = 1UL << (12 + 9 * lev);
			if (pa & (sz - 1))
				return 0;
			*pte = e;
			*slot = p;
			pa += va & (sz - 1);
			return pvm_gpa(v, pa, 1) ? pa : 0;
		}
		t = pa;
	}
	return 0;
}
unsigned long pvm_translate(struct riscv_pvm_vcpu *v, unsigned long va,
			    unsigned long *pte)
{
	unsigned long *slot;
	return walk(v, va, pte, &slot);
}
int pvm_page_fault(struct riscv_pvm_vcpu *v, unsigned long cause,
		   unsigned long va)
{
	unsigned long pte = 0, *slot, gpa, flags, access;
	void *backing;
	if ((va & PAGE_MASK) == PVM_GATE_VA || (va & PAGE_MASK) == PVM_STATE_VA)
		return 0;
	gpa = walk(v, va, &pte, &slot);
	if (!gpa)
		return 0;
	if (!v->supervisor && !(pte & 0x10))
		return 0;
	if (v->supervisor && (pte & 0x10) &&
	    (cause == 12 || !(v->csr[0] & SR_SUM)))
		return 0;
	access = cause == 12 ? 8 : cause == 15 ? 4 : 2;
	if (cause == 13 && (v->csr[0] & PVM_MXR) && (pte & 8))
		access = 8;
	if (!(pte & access))
		return 0;
	pte |= 0x40;
	if (cause == 15)
		pte |= 0x80;
	if (slot)
		WRITE_ONCE(*slot, pte);
	backing = pvm_gpa(v, gpa, 1);
	if (!backing)
		return 0;
	flags = (pte & 0x3ff) | 0x51;
	if (!(pte & 0x80))
		flags &= ~4UL;
	if ((v->csr[0] & PVM_MXR) && (pte & 8))
		flags |= 2;
	if (v->supervisor && (pte & 0x10))
		flags &= ~8UL;
	if (map_page(v->supervisor ? &v->kernel : &v->user, va & PAGE_MASK,
		     virt_to_phys(backing) & PAGE_MASK, flags))
		return -ENOMEM;
	v->faults++;
	v->n_fill++; /* D2: successful shadow demand-fill */

	return 1;
}
