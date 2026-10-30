// SPDX-License-Identifier: GPL-2.0
/* Stage-B VMM: boots the real guest Linux through the S-mode PVM
 * KVM baseline. Loads Image/DTB/initramfs into the guest RAM window,
 * then services SBI console exits (legacy putchar and DBCN write)
 * with strict GPA validation -- guest addresses are never used as
 * host pointers. SRST becomes a clean exit. */
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
#define RAM_SIZE 0x07e00000UL
#define INITRD_GPA 0x85000000UL
#define DTB_GPA 0x87e00000UL

static unsigned char *ram;

static void die(const char *what)
{
	fprintf(stderr, "STAGE-B: FAIL (%s) errno=%d\n", what, errno);
	exit(1);
}

static void load_blob(const char *path, unsigned long gpa, long *out_len,
		      long maxlen)
{
	FILE *f = fopen(path, "r");
	long n;
	if (!f)
		die(path);
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (n <= 0 || n > maxlen)
		die("blob size");
	if (gpa < RAM_BASE || gpa + n > RAM_BASE + RAM_SIZE)
		die("blob bounds");
	if (fread(ram + (gpa - RAM_BASE), 1, n, f) != (size_t)n)
		die("blob read");
	fclose(f);
	if (out_len)
		*out_len = n;
}

/* Translate a guest physical range into OUR window, refusing any
 * overflow or out-of-slot access. */
static unsigned char *gpa_check(uint64_t gpa, uint64_t len)
{
	if (len > RAM_SIZE)
		return NULL;
	if (gpa < RAM_BASE || gpa - RAM_BASE > RAM_SIZE - len)
		return NULL;
	return ram + (gpa - RAM_BASE);
}

int main(int argc, char **argv)
{
	const char *image = argc > 1 ? argv[1] : "/guest-Image";
	const char *dtb = argc > 2 ? argv[2] : "/guest.dtb";
	const char *initrd = argc > 3 ? argv[3] : "/guest-initramfs.cpio.gz";
	int kfd, vmfd, vfd;
	long msize;
	struct kvm_userspace_memory_region region;
	struct kvm_run *run;
	int runs = 0;

	kfd = open("/dev/kvm", O_RDWR);
	if (kfd < 0)
		die("open /dev/kvm");
	if (ioctl(kfd, KVM_GET_API_VERSION, 0) != 12)
		die("api version");
	vmfd = ioctl(kfd, KVM_CREATE_VM, 0);
	if (vmfd < 0)
		die("KVM_CREATE_VM");

	ram = mmap(NULL, RAM_SIZE, PROT_READ | PROT_WRITE,
		   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (ram == MAP_FAILED)
		die("guest RAM mmap");

	load_blob(image, RAM_BASE, NULL, 0x02000000);
	load_blob(dtb, DTB_GPA, NULL, 0x10000);
	load_blob(initrd, INITRD_GPA, NULL, 0x02000000);

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
		unsigned char *p;

		if (ioctl(vfd, KVM_RUN, 0) < 0) {
			if (errno == EINTR)
				continue;
			die("KVM_RUN");
		}
		runs++;
		switch (run->exit_reason) {
		case KVM_EXIT_RISCV_SBI:
			if (run->riscv_sbi.extension_id == 1) {
				/* legacy putchar: one char in a0 */
				char c = (char)run->riscv_sbi.args[0];
				if (write(1, &c, 1) != 1)
					die("putchar");
				run->riscv_sbi.ret[0] = 0;
				run->riscv_sbi.ret[1] = 0;
			} else if (run->riscv_sbi.extension_id == 0x4442434e &&
				   run->riscv_sbi.function_id == 0) {
				/* DBCN write on RV64: a0=len, a1=full-XLEN
				 * phys_lo, a2 MUST be zero.  A nonzero a2 or a
				 * bad range is rejected; guest addresses are
				 * never used as host pointers. */
				uint64_t len = run->riscv_sbi.args[0];
				uint64_t gpa = run->riscv_sbi.args[1];
				if (run->riscv_sbi.args[2] != 0) {
					run->riscv_sbi.ret[0] = (uint64_t)-3;
				} else {
					p = gpa_check(gpa, len);
					if (!p) {
						fprintf(stderr,
							"STAGE-B: DBCN bad gpa 0x%llx len %llu\n",
							(unsigned long long)gpa,
							(unsigned long long)len);
						run->riscv_sbi.ret[0] =
							(uint64_t)-3;
						run->riscv_sbi.ret[1] = 0;
					} else {
						long w = write(1, p, len);
						/* SBI DBCN contract: a0=error, a1=value=bytes
						 * written.  value<len makes the kernel's
						 * console driver retry the whole write,
						 * which loops the banner forever. */
						if (w < 0) {
							run->riscv_sbi.ret[0] =
								(uint64_t)-1;
							run->riscv_sbi.ret[1] =
								0;
						} else {
							run->riscv_sbi.ret[0] =
								0;
							run->riscv_sbi.ret[1] =
								(uint64_t)w;
						}
					}
				}
			} else {
				fprintf(stderr,
					"STAGE-B: SBI exit ext=0x%llx fid=%llu unhandled\n",
					(unsigned long long)
						run->riscv_sbi.extension_id,
					(unsigned long long)
						run->riscv_sbi.function_id);
				run->riscv_sbi.ret[0] =
					(uint64_t)-2; /* NOT_SUPPORTED */
				run->riscv_sbi.ret[1] = 0;
			}
			break;
		case KVM_EXIT_SYSTEM_EVENT:
			printf("\nSTAGE-B: SYSTEM_EVENT type=%u ndata=%u data0=0x%llx runs=%d\n",
			       run->system_event.type, run->system_event.ndata,
			       run->system_event.ndata ?
				       run->system_event.data[0] :
				       0,
			       runs);
			printf("STAGE-B: %s (guest reached SBI SRST under /dev/kvm)\n",
			       run->system_event.type ==
					       KVM_SYSTEM_EVENT_SHUTDOWN ?
				       "PASS" :
				       "RESET/OTHER");
			return run->system_event.type ==
					       KVM_SYSTEM_EVENT_SHUTDOWN ?
				       0 :
				       2;
		default:
			fprintf(stderr,
				"STAGE-B: unexpected exit reason=%d runs=%d\n",
				run->exit_reason, runs);
			return 1;
		}
	}
}
