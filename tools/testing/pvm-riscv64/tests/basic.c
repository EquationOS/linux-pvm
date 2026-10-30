// SPDX-License-Identifier: GPL-2.0
/* mmap/fork/signal test suite for the U-mode guest /init acceptance.
 * Every check prints PASS/FAIL; exits 0 only with zero failures. */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int fails;

static void check(const char *what, int ok)
{
	printf("%s: %s\n", what, ok ? "PASS" : "FAIL");
	if (!ok)
		fails++;
}

static void test_anon_mmap(void)
{
	char *p = mmap(NULL, 0x2000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED) {
		check("mmap-anon", 0);
		return;
	}
	check("mmap-anon", 1);

	strcpy(p, "hello-pvm-riscv64");
	check("mmap-write-readback", strcmp(p, "hello-pvm-riscv64") == 0);
	check("mmap-zero-page", p[0x1000] == 0);
	check("mprotect-rw", mprotect(p, 0x2000, PROT_READ | PROT_WRITE) == 0);
	check("munmap", munmap(p, 0x2000) == 0);
}

static void test_mprotect_ro_sigsegv(void)
{
	/* child writes an RO page: must die with SIGSEGV (signal 11) */
	char *p = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	pid_t pid;
	int st = -1;

	if (p == MAP_FAILED) {
		check("ro-sigsegv-mmap", 0);
		return;
	}
	strcpy(p, "ro");
	if (mprotect(p, 0x1000, PROT_READ) != 0) {
		check("ro-sigsegv-mprotect", 0);
		munmap(p, 0x1000);
		return;
	}
	pid = fork();
	if (pid < 0) {
		check("ro-sigsegv-fork", 0);
		return;
	}
	if (pid == 0) {
		p[0] = 'x'; /* must NOT return */
		_exit(0);
	}
	check("ro-sigsegv-wait", waitpid(pid, &st, 0) == pid);
	check("ro-sigsegv-signal", WIFSIGNALED(st) && WTERMSIG(st) == SIGSEGV);
	munmap(p, 0x1000);
}

static volatile sig_atomic_t got_usr1;

static void handler(int sig)
{
	got_usr1 = sig;
}

static void test_sigusr1(void)
{
	struct sigaction sa = {0};
	pid_t pid;
	int st = -1;

	sa.sa_handler = handler;
	if (sigaction(SIGUSR1, &sa, NULL) != 0) {
		check("sigusr1-sigaction", 0);
		return;
	}
	check("sigusr1-sigaction", 1);
	pid = fork();
	if (pid < 0) {
		check("sigusr1-fork", 0);
		return;
	}
	if (pid == 0) {
		kill(getppid(), SIGUSR1);
		_exit(0);
	}
	waitpid(pid, &st, 0);
	check("sigusr1-delivered", got_usr1 == SIGUSR1);
}

static void test_nanosleep(void)
{
	struct timespec before, after, req = {.tv_sec = 0, .tv_nsec = 50000000};

	if (clock_gettime(CLOCK_MONOTONIC, &before) != 0) {
		check("nanosleep-clock_gettime", 0);
		return;
	}
	if (clock_nanosleep(CLOCK_MONOTONIC, 0, &req, NULL) != 0) {
		check("nanosleep-clock_nanosleep", 0);
		return;
	}
	clock_gettime(CLOCK_MONOTONIC, &after);
	long long ns =
	    (after.tv_sec - before.tv_sec) * 1000000000LL + (after.tv_nsec - before.tv_nsec);
	check("nanosleep-elapsed>=40ms", ns >= 40000000LL);
	check("nanosleep-sane(<5s)", ns < 5000000000LL);
}

static void test_arith(void)
{
	volatile long long a = 0x123456789ABCDEFLL, b = 0x13579BDF2468ACELL;
	volatile double x = 3.14159, y = 2.71828;

	check("int-add", a + b == 0x258bf257bf258bdLL);
	check("int-divmod-self", (b / a) * a + b % a == b);
	check("int-shifts", (a << 3) == a * 8);
	check("int-shr", (a >> 4) == a / (1LL << 4));
	check("fp-add", (x + y) > 5.85 && (x + y) < 5.86);
	check("fp-mul", (x * y) > 8.53 && (x * y) < 8.54);
	check("fp-div", (x / y) > 1.15 && (x / y) < 1.16);
}

static void test_user_boundary(void)
{
	/* These must stay guest-user events, even with a forged host EID. */
	register long a0 asm("a0") = 0;
	register long a1 asm("a1") = 0;
	register long a6 asm("a6") = 0x17088209;
	register long a7 asm("a7") = 0x70766d;
	int st = 0;
	pid_t pid;

	asm volatile("ecall" : "+r"(a0), "+r"(a1) : "r"(a6), "r"(a7) : "memory");
	check("user-forged-host-ecall-enosys", a0 == -ENOSYS);

	/* User-mode PV_RETURN forgery (fid 14): a0 targets the protected
	 * state VA.  Identity is USER, so this MUST be delivered to the
	 * guest kernel as a plain user ecall and answered -ENOSYS (the
	 * kernel has no such syscall), never executed as a supervisor
	 * PV_RETURN.  Independent pass name for E2 identity proof. */
	{
		register long b0 asm("a0") = 0x3ffffff000;
		register long b1 asm("a1") = 0;
		register long b6 asm("a6") = 0x1708820e;
		register long b7 asm("a7") = 0x70766d;

		asm volatile("ecall" : "+r"(b0), "+r"(b1)
			     : "r"(b6), "r"(b7) : "memory");
		check("user-forged-pv-return-enosys", b0 == -ENOSYS);
	}

	pid = fork();
	if (pid < 0) {
		check("user-csr-fork", 0);
		return;
	}
	if (!pid) {
		unsigned long value;
		asm volatile("csrr %0, satp" : "=r"(value));
		_exit(0);
	}
	check("user-csr-wait", waitpid(pid, &st, 0) == pid);
	check("user-csr-sigill", WIFSIGNALED(st) && WTERMSIG(st) == SIGILL);
	pid = fork();
	if (pid < 0) {
		check("user-host-memory-fork", 0);
		return;
	}
	if (!pid) {
		volatile unsigned long value = *(volatile unsigned long *)0x80000000UL;
		(void)value;
		_exit(0);
	}
	check("user-host-memory-wait", waitpid(pid, &st, 0) == pid);
	check("user-host-memory-sigsegv", WIFSIGNALED(st) && WTERMSIG(st) == SIGSEGV);
}

static void test_file_mmap(void)
{
	int fd = open("/etc/os-release-init-test", O_RDONLY);
	char buf[32];
	char *p;

	if (fd < 0) {
		check("mmap-file-open", 0);
		return;
	}
	p = mmap(NULL, 0x1000, PROT_READ, MAP_PRIVATE, fd, 0);
	if (p == MAP_FAILED) {
		check("mmap-file", 0);
		close(fd);
		return;
	}
	check("mmap-file", 1);
	memcpy(buf, p, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = 0;
	check("mmap-file-content", buf[0] == 'P' && buf[1] == 'V');
	munmap(p, 0x1000);
	close(fd);
}

static void test_fork_wait(void)
{
	pid_t pid = fork();
	int st = -1;
	char *p;

	if (pid < 0) {
		check("fork", 0);
		return;
	}
	if (pid == 0)
		_exit(42);
	check("fork", 1);
	check("waitpid", waitpid(pid, &st, 0) == pid);
	check("wait-exit-code", WIFEXITED(st) && WEXITSTATUS(st) == 42);

	p = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED) {
		check("cow-mmap", 0);
		return;
	}
	strcpy(p, "parent");
	pid = fork();
	if (pid < 0) {
		check("cow-fork", 0);
		return;
	}
	if (pid == 0) {
		strcpy(p, "childwrote");
		_exit(0);
	}
	waitpid(pid, &st, 0);
	check("fork-cow-parent-view", strcmp(p, "parent") == 0);
	munmap(p, 0x1000);
}

int main(void)
{
	printf("== init-tests: mmap/fork/signal/arith suite ==\n");
	test_anon_mmap();
	test_mprotect_ro_sigsegv();
	test_sigusr1();
	test_nanosleep();
	test_arith();
	test_user_boundary();
	test_file_mmap();
	test_fork_wait();
	printf("%s\n", fails == 0 ? "INIT-TESTS-ALL-PASS" : "INIT-TESTS-FAIL");
	return fails == 0 ? 0 : 1;
}
