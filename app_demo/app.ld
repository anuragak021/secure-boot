/* Demo application: lives in the app slot, payload starts after the 0x400 header area. */
ENTRY(Reset_Handler)

_estack = 0x24080000;
_Min_Stack_Size = 0x2000;

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08020400, LENGTH = 0xDFC00 /* up to the end of bank 1 */
  RAM   (xrw) : ORIGIN = 0x24000000, LENGTH = 512K
}

SECTIONS
{
  /* 166 vectors = 664 bytes -> next power of two is 1024; origin is 1 KB aligned. */
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

ASSERT(ORIGIN(FLASH) % 1024 == 0, "app vector table must be 1 KB aligned (VTOR rule)")
ASSERT(SIZEOF(.isr_vector) <= 1024, "vector table larger than its alignment")
ASSERT(ORIGIN(FLASH) == 0x08020400, "app must start right after the 0x400 header area")
