// SPDX-License-Identifier: GPL-2.0
/* Kill the HOST VMM during a live Linux getpid loop. The host /init
 * separately checks that a fresh VM works after the child is reaped. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <time.h>
#include <sys/wait.h>

static long long now_ms(void)
{
	struct timespec t;
	if (clock_gettime(CLOCK_MONOTONIC, &t))
		return -1;
	return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static int forward(const char *p, size_t n)
{
	while (n) {
		ssize_t r = write(STDOUT_FILENO, p, n);
		if (r < 0 && errno == EINTR)
			continue;
		if (r <= 0)
			return -1;
		p += r;
		n -= r;
	}
	return 0;
}

int main(void)
{
	int fd[2], st = 0, ready = 0, bad = 0, reaped = 0;
	char buf[4096], line[1024];
	size_t used = 0;
	long long start = now_ms(), ready_at = 0;
	pid_t pid;

	if (start < 0 || pipe(fd))
		return 1;
	pid = fork();
	if (pid < 0)
		return 1;
	if (!pid) {
		close(fd[0]);
		if (dup2(fd[1], 1) < 0 || dup2(fd[1], 2) < 0)
			_exit(126);
		close(fd[1]);
		execl("/pvm-vmm", "pvm-vmm", NULL);
		_exit(127);
	}
	close(fd[1]);
	if (fcntl(fd[0], F_SETFL, O_NONBLOCK) < 0)
		bad = 1;
	while (!bad) {
		struct pollfd p = { .fd = fd[0], .events = POLLIN };
		long long now = now_ms();
		pid_t w = waitpid(pid, &st, WNOHANG);
		int r;
		ssize_t n;
		size_t i;

		if (w == pid) { reaped = 1; bad = 1; break; }
		if (w < 0 && errno != EINTR) { bad = 1; break; }
		if (now < 0 || now - start > 120000) { bad = 1; break; }
		if (ready && now - ready_at >= 100)
			break;
		r = poll(&p, 1, ready ? 10 : 250);
		if (r < 0) {
			if (errno == EINTR) continue;
			bad = 1; break;
		}
		if (!r) continue;
		n = read(fd[0], buf, sizeof(buf));
		if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
		if (n <= 0 || forward(buf, n)) { bad = 1; break; }
		for (i = 0; i < (size_t)n; i++) {
			if (buf[i] != '\n') {
				if (used < sizeof(line) - 1) line[used++] = buf[i];
				continue;
			}
			line[used] = 0;
			if (!ready && strstr(line, "FASTLOOP: READY")) {
				ready = 1;
				ready_at = now_ms();
			}
			if (strstr(line, "FAIL") || strstr(line, "panic") ||
			    strstr(line, "Oops") || strstr(line, "emulation failed"))
				bad = 1;
			used = 0;
		}
	}
	printf("FASTLOOP-HOST: ready-seen %s\n", ready ? "PASS" : "FAIL");
	printf("FASTLOOP-HOST: no-fail-in-window %s\n", !bad ? "PASS" : "FAIL");
	if (reaped || kill(pid, bad ? SIGKILL : SIGTERM))
		bad = 1;
	if (!reaped) {
		pid_t w;
		do { w = waitpid(pid, &st, 0); } while (w < 0 && errno == EINTR);
		if (w != pid) bad = 1;
	}
	close(fd[0]);
	if (!WIFSIGNALED(st) || WTERMSIG(st) != SIGTERM)
		bad = 1;
	printf("FASTLOOP-HOST: child-signaled %s\n", !bad ? "PASS" : "FAIL");
	printf("FASTLOOP-HOST: %s\n", !bad ? "ALL-PASS" : "SOME-FAILED");
	return bad ? 1 : 0;
}
