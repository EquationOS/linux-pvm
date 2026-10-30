// SPDX-License-Identifier: GPL-2.0
/* Stage-C negative tests for the host-control surface:
 *  N1  KVM_SET_USER_MEMORY_REGION with a wrong GPA is rejected.
 *  N2  a memslot with flags set is rejected.
 *  N3  a second memslot is rejected.
 *  N4  a SEPARATE child process creates its own VM and runs an
 *      infinite U-mode loop; the parent SIGTERMs it, waits, and then
 *      successfully creates and runs a fresh VM to the stage-C magic
 *      (KVM_RUN is interruptible by host signals, close releases
 *      everything, and the host keeps creating VMs).
 * The child forks BEFORE any kvm fd exists in the child (execs this
 * binary with "child") so no KVM mm mismatch is involved. */
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <signal.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <linux/kvm.h>

#define RAM_BASE 0x80200000UL
#define RAM_SIZE 0x07e00000UL
#define PVM_STAGE_C_MAGIC 0x50564D4B564D4D43UL
#define PVM_STAGE_D_MAGIC 0x50564D4B564D4B44UL
#define PVM_STAGE_D2_MAGIC 0x50564D4B564D4D55UL

static void die(const char *what)
{
	fprintf(stderr, "NEG: FAIL (%s) errno=%d\n", what, errno);
	exit(1);
}

static int new_vm(void)
{
	int kfd = open("/dev/kvm", O_RDWR);
	int vmfd;

	if (kfd < 0)
		die("open /dev/kvm");
	vmfd = ioctl(kfd, KVM_CREATE_VM, 0);
	if (vmfd < 0)
		die("KVM_CREATE_VM");
	close(kfd);
	return vmfd;
}

static int set_slot(int vmfd, unsigned slot, unsigned flags,
		    uint64_t gpa, uint64_t size, void *va)
{
	struct kvm_userspace_memory_region r = {
		.slot = slot,
		.flags = flags,
		.guest_phys_addr = gpa,
		.memory_size = size,
		.userspace_addr = (uint64_t)va,
	};
	return ioctl(vmfd, KVM_SET_USER_MEMORY_REGION, &r);
}

static int run_payload_magic(const char *path, uint64_t magic)
{
	int vmfd = new_vm(), vfd, kfd = open("/dev/kvm", O_RDWR);
	long msz;
	struct kvm_run *run;
	static unsigned char *ram;
	FILE *f;
	long n;
	int rc = 1;

	ram = mmap(NULL, RAM_SIZE, PROT_READ | PROT_WRITE,
		   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (ram == MAP_FAILED)
		die("ram mmap");
	f = fopen(path, "r");
	if (!f)
		die(path);
	fseek(f, 0, SEEK_END);n = ftell(f);fseek(f, 0, SEEK_SET);
	if (n <= 0 || n > 0x8000)
		die("payload size");
	if (fread(ram, 1, n, f) != (size_t)n)
		die("payload read");
	fclose(f);
	if (set_slot(vmfd, 0, 0, RAM_BASE, RAM_SIZE, ram) < 0)
		die("memslot");
	vfd = ioctl(vmfd, KVM_CREATE_VCPU, 0);
	if (vfd < 0)
		die("vcpu");
	msz = ioctl(kfd, KVM_GET_VCPU_MMAP_SIZE, 0);
	run = mmap(NULL, msz, PROT_READ | PROT_WRITE, MAP_SHARED, vfd, 0);
	if (run == MAP_FAILED)
		die("run mmap");
	for (;;) {
		if (ioctl(vfd, KVM_RUN, 0) < 0) {
			if (errno == EINTR)
				continue;
			die("KVM_RUN");
		}
		if (run->exit_reason == KVM_EXIT_SYSTEM_EVENT &&
		    run->system_event.type == KVM_SYSTEM_EVENT_SHUTDOWN) {
			uint64_t d0 = run->system_event.ndata ?
				run->system_event.data[0] : 0;
			printf("NEG: SYS_EVT data0=0x%llx (%s)\n",
			       (unsigned long long)d0, path);
			rc = d0 == magic ? 0 : 1;
			break;
		}
		if (run->exit_reason == KVM_EXIT_RISCV_SBI &&
		    run->riscv_sbi.extension_id == 0x4442434e) {
			run->riscv_sbi.ret[0] = 0;
			run->riscv_sbi.ret[1] = 0;
			continue;
		}
		printf("NEG: unexpected exit %d (%s)\n", run->exit_reason, path);
		rc = 1;
		break;
	}
	munmap(run, msz);close(vfd);close(vmfd);munmap(ram, RAM_SIZE);
	return rc;
}

/* run payload.bin to the stage-C magic; returns 0 on exact match */
static int run_payload(const char *path)
{
	int vmfd = new_vm(), vfd, kfd = open("/dev/kvm", O_RDWR);
	long msz;
	struct kvm_run *run;
	static unsigned char *ram;
	FILE *f;
	long n;
	int rc = 1;

	ram = mmap(NULL, RAM_SIZE, PROT_READ | PROT_WRITE,
		   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (ram == MAP_FAILED)
		die("ram mmap");
	f = fopen(path, "r");
	if (!f)
		die(path);
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (n <= 0 || n > 0x2000)
		die("payload size");
	if (fread(ram, 1, n, f) != (size_t)n)
		die("payload read");
	fclose(f);
	if (set_slot(vmfd, 0, 0, RAM_BASE, RAM_SIZE, ram) < 0)
		die("good memslot rejected");

	vfd = ioctl(vmfd, KVM_CREATE_VCPU, 0);
	if (vfd < 0)
		die("KVM_CREATE_VCPU");
	msz = ioctl(kfd, KVM_GET_VCPU_MMAP_SIZE, 0);
	run = mmap(NULL, msz, PROT_READ | PROT_WRITE, MAP_SHARED, vfd, 0);
	if (run == MAP_FAILED)
		die("run mmap");

	for (;;) {
		if (ioctl(vfd, KVM_RUN, 0) < 0) {
			if (errno == EINTR)
				continue;
			die("KVM_RUN");
		}
		if (run->exit_reason == KVM_EXIT_SYSTEM_EVENT &&
		    run->system_event.type == KVM_SYSTEM_EVENT_SHUTDOWN) {
			uint64_t d0 = run->system_event.ndata ?
				run->system_event.data[0] : 0;
			printf("NEG: SYSTEM_EVENT data0=0x%llx\n",
			       (unsigned long long)d0);
			rc = d0 == PVM_STAGE_C_MAGIC ? 0 : 1;
			break;
		}
		printf("NEG: unexpected exit %d\n", run->exit_reason);
		break;
	}
	munmap(run, msz);
	close(vfd);
	close(vmfd);
	munmap(ram, RAM_SIZE);
	return rc;
}

int main(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "mmu")) {
		int rc = run_payload_magic("/payload-mmu.bin", PVM_STAGE_D2_MAGIC);
		printf("STAGE-MMU: %s\n", rc == 0 ? "PASS" : "FAIL");
		return rc;
	}

	if (argc > 1 && !strcmp(argv[1], "child")) {
		/* Own VM.  Phase 1: the guest reports readiness through a
		 * REAL exit (SRST private type, marker "READY11"); we write
		 * "R" to fd 3 so the parent knows KVM_RUN has actually
		 * executed guest code, then resume -- the guest then spins
		 * in U-mode until the parent SIGTERMs us. */
		int vmfd = new_vm(), vfd, kfd = open("/dev/kvm", O_RDWR);
		long msz;
		struct kvm_run *run;
		static unsigned char *ram;
		FILE *f;
		long n;

		ram = mmap(NULL, RAM_SIZE, PROT_READ | PROT_WRITE,
			   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
		if (ram == MAP_FAILED)
			die("child ram mmap");
		f = fopen("/payload-loop.bin", "r");
		if (!f)
			die("child payload open");
		fseek(f, 0, SEEK_END);
		n = ftell(f);
		fseek(f, 0, SEEK_SET);
		if (n <= 0 || n > 0x2000)
			die("child payload size");
		if (fread(ram, 1, n, f) != (size_t)n)
			die("child payload read");
		fclose(f);
		if (set_slot(vmfd, 0, 0, RAM_BASE, RAM_SIZE, ram) < 0)
			die("child memslot");
		vfd = ioctl(vmfd, KVM_CREATE_VCPU, 0);
		if (vfd < 0)
			die("child vcpu");
		msz = ioctl(kfd, KVM_GET_VCPU_MMAP_SIZE, 0);
		run = mmap(NULL, msz, PROT_READ | PROT_WRITE, MAP_SHARED, vfd, 0);
		if (run == MAP_FAILED)
			die("child run mmap");

		for (;;) {
			if (ioctl(vfd, KVM_RUN, 0) < 0) {
				if (errno == EINTR)
					continue;
				die("child KVM_RUN");
			}
			if (run->exit_reason == KVM_EXIT_RISCV_SBI &&
			    run->riscv_sbi.extension_id == 0x4442434e &&
			    run->riscv_sbi.function_id == 2 &&
			    run->riscv_sbi.args[0] == 'R') {
				/* DBCN write_byte carries the byte in a0.
				 * Acknowledge, signal readiness via the pipe;
				 * the next KVM_RUN resumes past the ecall
				 * (sbi_pending PC+4) into the infinite loop. */
				run->riscv_sbi.ret[0] = 0;
				run->riscv_sbi.ret[1] = 0;
				{
					char c = 'R';
					if (write(3, &c, 1) != 1)
						die("child ready write");
				}
				continue;
			}
			printf("NEG: child unexpected exit %d\n", run->exit_reason);
			return 3;
		}
	}

	/* N1: wrong GPA */
	{
		int vmfd = new_vm();
		void *va = mmap(NULL, RAM_SIZE, PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS, -1, 0);

		if (set_slot(vmfd, 0, 0, RAM_BASE + 0x1000, RAM_SIZE, va) == 0) {
			printf("NEG-N1: FAIL (bad GPA accepted)\n");
			return 1;
		}
		printf("NEG-N1: PASS (bad GPA rejected errno=%d)\n", errno);
		/* N2: flags set */
		if (set_slot(vmfd, 0, KVM_MEM_LOG_DIRTY_PAGES, RAM_BASE,
			     RAM_SIZE, va) == 0) {
			printf("NEG-N2: FAIL (flags accepted)\n");
			return 1;
		}
		printf("NEG-N2: PASS (flags rejected errno=%d)\n", errno);
		/* N3: second slot */
		if (set_slot(vmfd, 0, 0, RAM_BASE, RAM_SIZE, va) < 0)
			die("good slot rejected");
		if (set_slot(vmfd, 1, 0, 0x88000000UL, 0x1000, va) == 0) {
			printf("NEG-N3: FAIL (second slot accepted)\n");
			return 1;
		}
		printf("NEG-N3: PASS (second slot rejected errno=%d)\n", errno);
		close(vmfd);
	}

	/* N0: capability honesty -- DESTROY_MEMORY_REGION_WORKS must NOT
	 * be advertised (the backend only supports MR_CREATE). */
	{
		int kfd = open("/dev/kvm", O_RDWR);
		long d;

		if (kfd < 0)
			die("N0 open");
		d = ioctl(kfd, KVM_CHECK_EXTENSION,
			  KVM_CAP_DESTROY_MEMORY_REGION_WORKS);
		if (d != 0) {
			printf("NEG-N0: FAIL (DESTROY advertised = %ld)\n", d);
			close(kfd);
			return 1;
		}
		printf("NEG-N0: PASS (DESTROY not advertised)\n");
		d = ioctl(kfd, KVM_CHECK_EXTENSION,
			  KVM_CAP_JOIN_MEMORY_REGIONS_WORKS);
		if (d != 0) {
			printf("NEG-N0: FAIL (JOIN advertised = %ld)\n", d);
			close(kfd);
			return 1;
		}
		printf("NEG-N0: PASS (JOIN not advertised)\n");
		{
			static const int unsupported[] = {
				KVM_CAP_READONLY_MEM, KVM_CAP_MANUAL_DIRTY_LOG_PROTECT2,
				KVM_CAP_IRQFD, KVM_CAP_SIGNAL_MSI, KVM_CAP_IRQ_ROUTING,
				KVM_CAP_COALESCED_MMIO, KVM_CAP_COALESCED_PIO,
			};
			unsigned int i;
			for (i = 0; i < sizeof(unsupported)/sizeof(unsupported[0]); i++)
				if (ioctl(kfd, KVM_CHECK_EXTENSION, unsupported[i]) != 0)
					die("unsupported capability advertised");
			if (ioctl(kfd, KVM_CHECK_EXTENSION, KVM_CAP_USER_MEMORY) != 1 ||
			    ioctl(kfd, KVM_CHECK_EXTENSION, KVM_CAP_NR_MEMSLOTS) != 1)
				die("supported capabilities incorrect");
			puts("NEG-N0: PASS (memory/IRQ capabilities match backend)");
		}
		close(kfd);
	}

	/* N4: child with its OWN VM runs forever; SIGTERM; then a fresh
	 * VM must still reach the stage-C magic */
	{
		int pfd[2];
		pid_t pid;
		int st;
		char c;

		if (pipe(pfd) < 0)
			die("pipe");
		pid = fork();
		if (pid < 0)
			die("fork");
		if (pid == 0) {
			close(pfd[0]);
			dup2(pfd[1], 3);
			close(pfd[1]);
			execl("/pvm-neg", "pvm-neg", "child", NULL);
			_exit(127);
		}
		close(pfd[1]);
		{
			struct pollfd pf = { .fd = pfd[0], .events = POLLIN };
			int pr = poll(&pf, 1, 5000);

			if (pr == 1 && (pf.revents & POLLIN) &&
			    read(pfd[0], &c, 1) == 1 && c == 'R') {
				/* ready */
			} else {
				printf("NEG-N4a: FAIL (no ready in 5s)\n");
				kill(pid, SIGKILL);
				waitpid(pid, &st, 0);
				return 1;
			}
		}
		close(pfd[0]);
		printf("NEG-N4a: guest loop is running (ready received)\n");
		usleep(300000);
		kill(pid, SIGTERM);
		if (waitpid(pid, &st, 0) != pid)
			die("waitpid");
		if (!WIFSIGNALED(st) || WTERMSIG(st) != SIGTERM) {
			printf("NEG-N4a: FAIL (termsig=%d)\n",
			       WIFSIGNALED(st) ? WTERMSIG(st) : 0);
			return 1;
		}
		printf("NEG-N4a: PASS (SIGTERM interrupted a live KVM_RUN)\n");
	}

	if (run_payload("/payload-c.bin") != 0) {
		printf("NEG-N4b: FAIL\n");
		return 1;
	}
	printf("NEG-N4b: PASS (fresh VM after SIGTERM reached the magic)\n");

	/* D-payload: D1 correctness assertions run under a fresh VM */
	{
		int vmfd = new_vm(), vfd, kfd = open("/dev/kvm", O_RDWR);
		long msz;
		struct kvm_run *run;
		static unsigned char *ram;
		FILE *f = fopen("/payload-d.bin", "r");
		long n;

		if (!f)
			die("payload-d open");
		fseek(f, 0, SEEK_END);n = ftell(f);fseek(f, 0, SEEK_SET);
		if (n <= 0 || n > 0x4000)
			die("payload-d size");
		ram = mmap(NULL, RAM_SIZE, PROT_READ | PROT_WRITE,
			   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
		if (ram == MAP_FAILED)
			die("payload-d ram");
		if (fread(ram, 1, n, f) != (size_t)n)
			die("payload-d read");
		fclose(f);
		if (set_slot(vmfd, 0, 0, RAM_BASE, RAM_SIZE, ram) < 0)
			die("payload-d memslot");
		vfd = ioctl(vmfd, KVM_CREATE_VCPU, 0);
		if (vfd < 0)
			die("payload-d vcpu");
		msz = ioctl(kfd, KVM_GET_VCPU_MMAP_SIZE, 0);
		run = mmap(NULL, msz, PROT_READ | PROT_WRITE, MAP_SHARED, vfd, 0);
		if (run == MAP_FAILED)
			die("payload-d run mmap");
		for (;;) {
			if (ioctl(vfd, KVM_RUN, 0) < 0) {
				if (errno == EINTR)
					continue;
				die("payload-d KVM_RUN");
			}
			if (run->exit_reason == KVM_EXIT_SYSTEM_EVENT &&
			    run->system_event.type == KVM_SYSTEM_EVENT_SHUTDOWN) {
				uint64_t d0 = run->system_event.ndata ?
					run->system_event.data[0] : 0;
				printf("STAGE-D: SYSTEM_EVENT data0=0x%llx\n",
				       (unsigned long long)d0);
				if (d0 == PVM_STAGE_D_MAGIC) {
					printf("STAGE-D: PASS (illegal inject + WARL + stats window verified)\n");
					printf("NEG: ALL-PASS\n");
					return 0;
				}
				printf("STAGE-D: FAIL (wrong marker 0x%llx)\n",
				       (unsigned long long)d0);
				return 1;
			}
			printf("STAGE-D: unexpected exit %d\n", run->exit_reason);
			return 1;
		}
	}
	return 1;
}
