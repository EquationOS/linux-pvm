# SPDX-License-Identifier: GPL-2.0
# PVM KVM userspace: smoke VMMs + payloads + guest-boot VMM.
CROSS ?= riscv64-linux-gnu-
CFLAGS = -static -O2 -Wall -Wextra
W := $(PVM_WORKDIR)

all: vmm vmm-linux vmm-neg payload.bin payload-c.bin payload-loop.bin

vmm: vmm.c
	$(CROSS)gcc $(CFLAGS) -o $@ $<

vmm-linux: vmm-linux.c
	$(CROSS)gcc $(CFLAGS) -o $@ $<

vmm-neg: vmm-neg.c
	$(CROSS)gcc $(CFLAGS) -o $@ $<

%.o: %.S
	$(CROSS)gcc -c -o $@ $<

payload.bin: payload.o
	$(CROSS)objcopy -O binary -j .text $< $@

payload-c.bin: payload-c.o
	$(CROSS)objcopy -O binary -j .text $< $@

payload-loop.bin: payload-loop.o
	$(CROSS)objcopy -O binary -j .text $< $@

clean:
	rm -f vmm vmm-linux vmm-neg *.o *.bin
