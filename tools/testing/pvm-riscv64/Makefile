# SPDX-License-Identifier: GPL-2.0
ROOT := $(abspath ../../..)
OUT ?= $(CURDIR)/out
CROSS_COMPILE ?= riscv64-linux-gnu-
CC := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
HOSTCC ?= gcc
CFLAGS := -mcmodel=medany -march=rv64imac_zicsr_zifencei -mabi=lp64 \
 -mno-relax -msmall-data-limit=0 -fno-builtin -fno-stack-protector \
 -O2 -g -Wall -Wextra -Werror -fno-pie -no-pie -nostdlib -nostdinc \
 -ffreestanding -Imonitor -I$(ROOT)/arch/riscv/include/uapi \
 -I$(ROOT)/include/uapi $(MONITOR_CFLAGS)

all: $(OUT)/monitor.bin

$(OUT):
	mkdir -p $@

$(OUT)/monitor.elf: monitor/host.c monitor/host.S monitor/host.ld monitor/trap.h $(ROOT)/arch/riscv/include/uapi/asm/pvm_para.h | $(OUT)
	$(CC) $(CFLAGS) -Wl,--build-id=none -Tmonitor/host.ld monitor/host.S monitor/host.c -o $@

$(OUT)/monitor.bin: $(OUT)/monitor.elf
	$(OBJCOPY) -O binary $< $@

$(OUT)/basic: tests/basic.c | $(OUT)
	$(CC) -static -O2 -Wall -Wextra -Werror -fno-strict-aliasing $< -o $@

$(OUT)/gen_init_cpio: $(ROOT)/usr/gen_init_cpio.c | $(OUT)
	$(HOSTCC) -O2 -Wall $< -o $@

initramfs: $(OUT)/basic $(OUT)/gen_init_cpio
	bash build-initramfs.sh "$(OUT)" "$(BUSYBOX)"

clean:
	rm -f $(OUT)/monitor.elf $(OUT)/monitor.bin $(OUT)/basic $(OUT)/gen_init_cpio $(OUT)/initramfs.cpio.gz

.PHONY: all initramfs clean
