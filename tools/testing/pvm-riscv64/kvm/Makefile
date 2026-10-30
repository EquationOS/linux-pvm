# SPDX-License-Identifier: GPL-2.0
# PVM KVM userspace: smoke VMMs + payloads + guest-boot VMM.
CROSS ?= riscv64-linux-gnu-
CFLAGS = -static -O2 -Wall -Wextra
# Payload assembly must be linked at the fixed load address with no
# GOT/PLT and no linker relaxation: la must resolve pc-relative at link
# time, never through R_RISCV_GOT_HI20.
ASFLAGS = -fno-pic -fno-pie -mno-relax
LDFLAGS_PAYLOAD = -nostdlib -static -no-pie -Wl,--no-relax,-e,_start
PAYLOAD_LDS := -Ttext=0x80200000
W := $(PVM_WORKDIR)
.SECONDARY:

all: vmm vmm-linux vmm-neg payload.bin payload-c.bin payload-loop.bin payload-d.bin payload-mmu.bin payload-pvforge.bin getpid-window fp-signal-test fast-loop-test fast-loop-parent

vmm: vmm.c
	$(CROSS)gcc $(CFLAGS) -o $@ $<

vmm-linux: vmm-linux.c
	$(CROSS)gcc $(CFLAGS) -o $@ $<

vmm-neg: vmm-neg.c
	$(CROSS)gcc $(CFLAGS) -o $@ $<

getpid-window: getpid-window.c
	$(CROSS)gcc -static -O2 -Wall -o $@ $<

fp-signal-test: fp-signal-test.c
	$(CROSS)gcc -static -O2 -Wall -o $@ $<

fast-loop-test: fast-loop-test.c
	$(CROSS)gcc -static -O2 -Wall -o $@ $<

fast-loop-parent: fast-loop-parent.c
	$(CROSS)gcc -static -O2 -Wall -Wextra -o $@ $<

# Common rule: assemble no-PIC, link to ELF at 0x80200000, objcopy .text
%.elf: %.S
	$(CROSS)gcc $(ASFLAGS) -c -o $*.o $<
	$(CROSS)gcc $(LDFLAGS_PAYLOAD) $(PAYLOAD_LDS) -o $@ $*.o

%.bin: %.elf
	$(CROSS)objcopy -O binary -j .text $< $@

payload-pvforge.elf: payload-pvforge.S
	$(CROSS)gcc $(ASFLAGS) -c -o $*.o $<
	$(CROSS)gcc $(LDFLAGS_PAYLOAD) $(PAYLOAD_LDS) -o $@ $*.o

guest-initramfs-d:
	bash build-guest-initramfs-d.sh

# verify: no unresolved relocations in any payload ELF
verify-payloads: payload.elf payload-c.elf payload-loop.elf payload-d.elf payload-mmu.elf
	@for e in $^; do \
		r=$$($(CROSS)readelf -r $$e | grep -c 'R_RISCV_' || true); \
		if [ "$$r" != "0" ]; then echo "RELOC-FAIL $$e ($$r)"; exit 1; fi; \
		echo "OK $$e (no GOT/PLT relocs)"; \
	done

clean:
	rm -f vmm vmm-linux vmm-neg *.o *.bin *.elf getpid-window fp-signal-test fast-loop-test fast-loop-parent fast-loop-parent
