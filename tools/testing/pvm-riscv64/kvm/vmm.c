// SPDX-License-Identifier: GPL-2.0
/* Stage-A minimal VMM: proves the S-mode /dev/kvm path end to end.
 * One VM, one vCPU, one memslot; runs the smoke payload and requires
 * the guest's SBI SRST ecall to come back as
 * KVM_EXIT_SYSTEM_EVENT(SHUTDOWN, ndata>=1, data[0]==MAGIC).
 * Prints STAGE-A: PASS only on that exact evidence. */
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/kvm.h>

#define RAM_BASE 0x80200000UL
#define RAM_SIZE 0x07e00000UL /* [0x80200000,0x88000000) = 126MB */
#define PVM_STAGE_A_MAGIC 0x50564D554D4F4445UL

static void die(const char *what)
{
	fprintf(stderr, "STAGE-A: FAIL (%s) errno=%d\n", what, errno);
	exit(1);
}

int main(void)
{
	int kfd, vmfd, vfd;
	long msize;
	struct kvm_userspace_memory_region region;
	struct kvm_run *run;
	unsigned char *ram, *payload;
	long plen;
	int kvm_runs = 0;
	FILE *pf;

	kfd = open("/dev/kvm", O_RDWR);
	if (kfd < 0)
		die("open /dev/kvm");
	/* variadic ioctl: the argument register must be explicitly zero,
	 * the kernel rejects KVM_GET_API_VERSION with a nonzero arg. */
	if (ioctl(kfd, KVM_GET_API_VERSION, 0) != 12)
		die("api version");

	vmfd = ioctl(kfd, KVM_CREATE_VM, 0);
	if (vmfd < 0)
		die("KVM_CREATE_VM");

	ram = mmap(NULL, RAM_SIZE, PROT_READ | PROT_WRITE,
		   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (ram == MAP_FAILED)
		die("guest RAM mmap");

	/* load the smoke payload at GPA RAM_BASE (slot offset 0) */
	pf = fopen("/pvm-payload.bin", "r");
	if (!pf)
		die("open payload");
	fseek(pf, 0, SEEK_END);
	plen = ftell(pf);
	fseek(pf, 0, SEEK_SET);
	if (plen <= 0 || plen > 0x1000)
		die("payload size");
	if (fread(ram, 1, plen, pf) != (size_t)plen)
		die("payload read");
	fclose(pf);
	payload = ram;

	region.slot = 0;
	region.flags = 0;
	region.guest_phys_addr = RAM_BASE;
	region.memory_size = RAM_SIZE;
	region.userspace_addr = (unsigned long)ram;
	if (ioctl(vmfd, KVM_SET_USER_MEMORY_REGION, &region) < 0)
		die("KVM_SET_USER_MEMORY_REGION");

	vfd = ioctl(vmfd, KVM_CREATE_VCPU, 0);
	if (vfd < 0)
		die("KVM_CREATE_VCPU");
	msize = ioctl(kfd, KVM_GET_VCPU_MMAP_SIZE, 0);
	if (msize <= 0)
		die("VCPU_MMAP_SIZE");
	run = mmap(NULL, msize, PROT_READ | PROT_WRITE, MAP_SHARED, vfd, 0);
	if (run == MAP_FAILED)
		die("run mmap");

	for (;;) {
		int rc = ioctl(vfd, KVM_RUN, 0);

		kvm_runs++;
		if (rc < 0)
			die("KVM_RUN");
		if (run->exit_reason == KVM_EXIT_SYSTEM_EVENT &&
		    run->system_event.type == KVM_SYSTEM_EVENT_SHUTDOWN) {
			uint64_t d0 = run->system_event.ndata >= 1 ?
					      run->system_event.data[0] :
					      0;
			printf("STAGE-A: SYSTEM_EVENT shutdown ndata=%u data0=0x%llx kvm_runs=%d\n",
			       run->system_event.ndata, (unsigned long long)d0,
			       kvm_runs);
			if (run->system_event.ndata >= 1 &&
			    d0 == PVM_STAGE_A_MAGIC) {
				printf("STAGE-A: PASS (guest U-mode ecall reached host via /dev/kvm)\n");
				return 0;
			}
			printf("STAGE-A: FAIL (magic mismatch)\n");
			return 1;
		}
		printf("STAGE-A: unexpected exit reason=%d (run %d)\n",
		       run->exit_reason, kvm_runs);
		return 1;
	}
	(void)payload;
}
