// SPDX-License-Identifier: GPL-2.0
/* Keep setup, raw syscall and capture in one asm block: libc calls may
 * legally clobber caller-saved FP registers. Compare every D register
 * and a nonzero FCSR, including after a handler clobbers FP/FCSR. */
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

static volatile sig_atomic_t got;
static void handler(int sig)
{
    __asm__ volatile("fmv.d.x f0, zero\n\t"
                     "fmv.d.x f1, zero\n\t"
                     "fmv.d.x f31, zero\n\t"
                     "csrw fcsr, zero" ::: "f0", "f1", "f31", "memory");
    got = sig;
}

static long roundtrip(const uint64_t *input, uint64_t *output,
                      long nr, long arg0, long arg1)
{
    register long r0 __asm__("a0") = arg0;
    register long r1 __asm__("a1") = arg1;
    register long r7 __asm__("a7") = nr;
    __asm__ volatile(
        "frcsr t1\n\t"
        "fld f0, 0(%[in])\n\t"
        "fld f1, 8(%[in])\n\t"
        "fld f2, 16(%[in])\n\t"
        "fld f3, 24(%[in])\n\t"
        "fld f4, 32(%[in])\n\t"
        "fld f5, 40(%[in])\n\t"
        "fld f6, 48(%[in])\n\t"
        "fld f7, 56(%[in])\n\t"
        "fld f8, 64(%[in])\n\t"
        "fld f9, 72(%[in])\n\t"
        "fld f10, 80(%[in])\n\t"
        "fld f11, 88(%[in])\n\t"
        "fld f12, 96(%[in])\n\t"
        "fld f13, 104(%[in])\n\t"
        "fld f14, 112(%[in])\n\t"
        "fld f15, 120(%[in])\n\t"
        "fld f16, 128(%[in])\n\t"
        "fld f17, 136(%[in])\n\t"
        "fld f18, 144(%[in])\n\t"
        "fld f19, 152(%[in])\n\t"
        "fld f20, 160(%[in])\n\t"
        "fld f21, 168(%[in])\n\t"
        "fld f22, 176(%[in])\n\t"
        "fld f23, 184(%[in])\n\t"
        "fld f24, 192(%[in])\n\t"
        "fld f25, 200(%[in])\n\t"
        "fld f26, 208(%[in])\n\t"
        "fld f27, 216(%[in])\n\t"
        "fld f28, 224(%[in])\n\t"
        "fld f29, 232(%[in])\n\t"
        "fld f30, 240(%[in])\n\t"
        "fld f31, 248(%[in])\n\t"
        "li t0, 0x43\n\t" /* valid RDN rounding mode + two exception flags */
        "csrw fcsr, t0\n\t"
        "ecall\n\t"
        "fsd f0, 0(%[out])\n\t"
        "fsd f1, 8(%[out])\n\t"
        "fsd f2, 16(%[out])\n\t"
        "fsd f3, 24(%[out])\n\t"
        "fsd f4, 32(%[out])\n\t"
        "fsd f5, 40(%[out])\n\t"
        "fsd f6, 48(%[out])\n\t"
        "fsd f7, 56(%[out])\n\t"
        "fsd f8, 64(%[out])\n\t"
        "fsd f9, 72(%[out])\n\t"
        "fsd f10, 80(%[out])\n\t"
        "fsd f11, 88(%[out])\n\t"
        "fsd f12, 96(%[out])\n\t"
        "fsd f13, 104(%[out])\n\t"
        "fsd f14, 112(%[out])\n\t"
        "fsd f15, 120(%[out])\n\t"
        "fsd f16, 128(%[out])\n\t"
        "fsd f17, 136(%[out])\n\t"
        "fsd f18, 144(%[out])\n\t"
        "fsd f19, 152(%[out])\n\t"
        "fsd f20, 160(%[out])\n\t"
        "fsd f21, 168(%[out])\n\t"
        "fsd f22, 176(%[out])\n\t"
        "fsd f23, 184(%[out])\n\t"
        "fsd f24, 192(%[out])\n\t"
        "fsd f25, 200(%[out])\n\t"
        "fsd f26, 208(%[out])\n\t"
        "fsd f27, 216(%[out])\n\t"
        "fsd f28, 224(%[out])\n\t"
        "fsd f29, 232(%[out])\n\t"
        "fsd f30, 240(%[out])\n\t"
        "fsd f31, 248(%[out])\n\t"
        "frcsr t0\n\t"
        "sd t0, 256(%[out])\n\t"
        "fscsr t1\n\t"
        : "+&r"(r0)
        : "r"(r1), "r"(r7), [in] "r"(input), [out] "r"(output)
        : "t0", "t1", "memory", "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "f10", "f11", "f12", "f13", "f14", "f15", "f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23", "f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31");
    return r0;
}

int main(void)
{
    uint64_t input[32], output[33];
    struct sigaction sa = { .sa_handler = handler };
    long pid = getpid(), rc;
    int i, failures = 0, ok;

    for (i = 0; i < 32; i++)
        input[i] = UINT64_C(0x3ff0000012340000) + (uint64_t)i * 0x10203;
    rc = roundtrip(input, output, SYS_getpid, 0, 0);
    ok = rc == pid && memcmp(input, output, sizeof(input)) == 0 && output[32] == 0x43;
    printf("fp-fcsr-syscall: %s\n", ok ? "PASS" : "FAIL");
    failures += !ok;
    if (sigaction(SIGUSR1, &sa, NULL)) {
        puts("fp-signal-setup: FAIL");
        return 1;
    }
    rc = roundtrip(input, output, SYS_kill, pid, SIGUSR1);
    ok = rc == 0 && got == SIGUSR1 && memcmp(input, output, sizeof(input)) == 0 && output[32] == 0x43;
    printf("fp-fcsr-signal: %s\n", ok ? "PASS" : "FAIL");
    failures += !ok;
    puts(failures ? "FP-TESTS-FAIL" : "FP-TESTS-PASS");
    return failures ? 1 : 0;
}
