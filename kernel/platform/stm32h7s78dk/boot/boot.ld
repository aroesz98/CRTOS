/*
 * boot.ld - the boot loader in the STM32H7S7's internal flash (64 KB at 0x08000000).
 *
 * Its data and stack are at the top of DTCM: the kernel builds its own stacks and heaps there
 * again, and keeps what must survive a reset (.noinit: the record of a panic) at the bottom
 * (kernel/platform/stm32h7s78dk/linker/crtos.ld checks that it ends below __boot_loader_ram,
 * which linker/memory.ld sets to the ORIGIN of RAM here).
 */
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
  RAM (rwx)   : ORIGIN = 0x2000C000, LENGTH = 16K
}

ENTRY(Reset_Handler)

SECTIONS
{
    .isr_vector : ALIGN(4)
    {
        KEEP(*(.isr_vector))
    } > FLASH

    .text : ALIGN(4)
    {
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    .ARM.exidx : ALIGN(4)
    {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    } > FLASH

    _sidata = LOADADDR(.data);

    .data : ALIGN(4)
    {
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    .bss (NOLOAD) : ALIGN(4)
    {
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    _estack = ORIGIN(RAM) + LENGTH(RAM);
    ASSERT(_ebss + 4096 <= _estack, "boot loader: no room for the stack")
}
