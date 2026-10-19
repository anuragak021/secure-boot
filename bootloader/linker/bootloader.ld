/* Bootloader: flash sector 0 (128 KB), all RAM in AXI SRAM. */
ENTRY(Reset_Handler)

_estack = 0x24080000;
_Min_Stack_Size = 0x2000;

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 128K
  RAM   (xrw) : ORIGIN = 0x24000000, LENGTH = 512K
}

SECTIONS
{
  .isr_vector : ALIGN(1024) { KEEP(*(.isr_vector)) . = ALIGN(4); } >FLASH
  .text : { *(.text*) *(.rodata*) KEEP(*(.init)) KEEP(*(.fini)) . = ALIGN(4); } >FLASH
  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
  .ARM : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } >FLASH
  .preinit_array : { PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array*)) PROVIDE_HIDDEN(__preinit_array_end = .); } >FLASH
  .init_array : { PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*)) PROVIDE_HIDDEN(__init_array_end = .); } >FLASH
  .fini_array : { PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array*)) PROVIDE_HIDDEN(__fini_array_end = .); } >FLASH

  _sidata = LOADADDR(.data);
  .data : { _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } >RAM AT> FLASH
  .bss : { _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } >RAM
  ._user_heap_stack : { . = ALIGN(8); . = . + _Min_Stack_Size; . = ALIGN(8); } >RAM

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
