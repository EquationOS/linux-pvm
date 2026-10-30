// SPDX-License-Identifier: GPL-2.0
/* D2 fixed-N getpid measurement: opens the host stats window via the
 * guest kernel's /proc/pvm_stats interface, runs N raw getpid loop,
 * closes the window, prints the counter deltas as JSON. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>

#define N_DEFAULT 10000
#define WARMUP 100

int main(int argc, char **argv)
{
	int n = argc > 1 ? atoi(argv[1]) : N_DEFAULT;
	int fd, i;
	long pid, expect;
	char buf[4096];
	int r;

	if (n <= 0) {
		fprintf(stderr, "PVMSTATS: FAIL n<=0\n");
		return 1;
	}
	fd = open("/proc/pvm_stats", O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "PVMSTATS: FAIL no /proc/pvm_stats (native?)\n");
		return 1;
	}

	/* warmup outside the window */
	expect = syscall(SYS_getpid);
	for (i = 0; i < WARMUP; i++)
		if (syscall(SYS_getpid) != expect) {
			fprintf(stderr, "PVMSTATS: FAIL warmup pid mismatch\n");
			return 1;
		}

	/* window opens: ONLY raw getpid inside, no printf/open/clock */
	if (write(fd, "reset", 5) != 5) {
		fprintf(stderr, "PVMSTATS: FAIL reset\n");
		return 1;
	}
	for (i = 0; i < n; i++) {
		pid = syscall(SYS_getpid);
		if (pid != expect) {
			/* pid mismatch: abort, the stats are void */
			if (write(fd, "read", 4) < 0) { /* best-effort close */ }
			fprintf(stderr, "PVMSTATS: FAIL pid mismatch at %d\n", i);
			return 1;
		}
	}
	if (write(fd, "read", 4) != 4) {
		fprintf(stderr, "PVMSTATS: FAIL read\n");
		return 1;
	}

	/* snapshot output (outside the window) */
	r = read(fd, buf, sizeof(buf) - 1);
	if (r <= 0) {
		fprintf(stderr, "PVMSTATS: FAIL snapshot read\n");
		return 1;
	}
	buf[r] = 0;
	printf("%s", buf);
	close(fd);
	return 0;
}
