/* M-mode mini host. Loaded by qemu -bios at 0x80000000. */
OUTPUT_ARCH(riscv)
ENTRY(_start)
PHDRS
{
  text PT_LOAD FLAGS(5);
  data PT_LOAD FLAGS(6);
}
SECTIONS
{
  . = 0x80000000;
  .text : { *(.text.host) *(.text .text.*) } :text
  .rodata : { *(.rodata .rodata.*) } :text
  . = ALIGN(0x1000);
  .data : { *(.data .data.*) *(.sdata .sdata.*) } :data
  .bss : { __bss_start = .; *(.bss .bss.*) *(.sbss .sbss.*) . = ALIGN(8); __bss_end = .; } :data
  . = ALIGN(0x1000);
  .hoststack (NOLOAD) : { . += 0x8000; } :data
  HOST_STACK_TOP = .;
}
