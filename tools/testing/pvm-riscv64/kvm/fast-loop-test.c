// SPDX-License-Identifier: GPL-2.0
/* E2 fast-loop guest test: warmup + verify PID, print READY, then
 * an infinite raw getpid loop with per-iteration PID validation.
 * Never exits on its own and installs NO signal handler -- the HOST
 * VMM process is SIGTERM'd from the parent, not this program. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static inline long raw_getpid(void)
{
	register long a0 asm("a0");
	register long a7 asm("a7") = 172; /* __NR_getpid on riscv64 */

	asm volatile ("ecall" : "=r"(a0) : "r"(a7) : "memory");
	return a0;
}


int main(int argc, char **argv)
{
	long pid = raw_getpid();
	int i;

	(void)argc; (void)argv;

	/* warmup: 1000 iterations with PID check */
	for (i = 0; i < 1000; i++) {
		if (raw_getpid() != pid) {
			fprintf(stderr, "FASTLOOP: FAIL warmup pid mismatch at %d\n", i);
			return 1;
		}
	}

	printf("FASTLOOP: READY pid=%ld\n", pid);
	fflush(stdout);
	fsync(1);

	/* infinite loop: never exits, never signals */
	for (;;)
		if (raw_getpid() != pid) {
			fprintf(stderr, "FASTLOOP: FAIL loop pid mismatch\n");
			return 1;
		}
	return 0;
}
