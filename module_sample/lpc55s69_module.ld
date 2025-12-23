/* Minimal module linker script for position-independent modules.
   All sections are placed in a single FLASH region for relocation.
*/

MEMORY
{
  FLASH (rwx) : ORIGIN = 0x00000000, LENGTH = 4K
}

ENTRY(module_entry)

SECTIONS
{
  /* ProgramInfo at start */
  .program_info ORIGIN(FLASH) : ALIGN(8)
  {
    KEEP(*(.program_info))
  } > FLASH

  /* Module descriptor */
  .module_header : ALIGN(8)
  {
    KEEP(*(.module_header))
  } > FLASH

  /* Code and read-only data */
  .text : ALIGN(8)
  {
    __entry_start__ = .;
    KEEP(*(.entry))
    __entry_end__ = .;
    PROVIDE(__entry_offset__ = __entry_start__);
    *(.text*)
    *(.rodata*)
    . = ALIGN(8);
  } > FLASH

  /* Initialized data */
  .data : ALIGN(8)
  {
    _sdata = .;
    *(.data*)
    . = ALIGN(8);
    _edata = .;
  } > FLASH
  _sidata = LOADADDR(.data);  /* Offset of .data in the binary image */

  /* Uninitialized data */
  .bss : ALIGN(8)
  {
    _sbss = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(8);
    _ebss = .;
  } > FLASH

  /* Calculate sizes */
  PROVIDE(__image_size__ = . - ORIGIN(FLASH));
  PROVIDE(__data_size__ = SIZEOF(.data));
  PROVIDE(__bss_size__ = SIZEOF(.bss));
  
  /* Stack managed by CRTOS kernel - these are just placeholders */
  PROVIDE(__StackTop = 0);
  PROVIDE(__MSPLIM = 0);
  PROVIDE(_end = _ebss);
}
