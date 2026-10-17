/*
 * crtos-xip.ld - CRTOS programs that run in place (XIP): code and constants stay where the
 * file is (the flash file system /flash0), only the data goes to the process's arena.
 *
 * A static position-independent executable (ld -pie) with two segments:
 *   text  from address 0, starting with the ELF and program headers - so the code's address
 *         is simply where the file lies plus the address here; read and execute
 *   data  from address 0x10000000: tables of pointers (.data.rel.ro, init/fini arrays), the
 *         GOT, data, bss; read and write
 * The code reaches every variable and constant through the GOT, whose address the program
 * keeps in r9 (-fPIC -msingle-pic-base -mpic-register=r9 -mno-pic-data-is-text-relative),
 * so no instruction depends on where the data lands. The loader (kernel/os/app.cpp) copies
 * the data, applies the R_ARM_RELATIVE relocations of .rel.dyn - a value below 0x10000000
 * points into the text, one above into the data - and sets r9. -z text makes the link fail
 * when something in the text segment would need a relocation.
 */
OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)

PHDRS
{
  text PT_LOAD FILEHDR PHDRS FLAGS(5);
  data PT_LOAD FLAGS(6);
  dynamic PT_DYNAMIC FLAGS(6);
}

SECTIONS
{
  . = SIZEOF_HEADERS;
  .crtos_app : { KEEP (*(.crtos_app)) } :text
  .text : {
    *(.text.unlikely .text.*_unlikely .text.unlikely.*)
    *(.text.hot .text.hot.*)
    *(.text .stub .text.* .gnu.linkonce.t.*)
    *(.glue_7t) *(.glue_7) *(.vfp11_veneer) *(.v4_bx)
  } :text
  .rodata : { *(.rodata .rodata.* .gnu.linkonce.r.*) } :text
  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } :text
  .ARM.exidx : {
    __exidx_start = .;
    *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    __exidx_end = .;
  } :text
  .hash : { *(.hash) } :text
  .gnu.hash : { *(.gnu.hash) } :text
  .dynsym : { *(.dynsym) } :text
  .dynstr : { *(.dynstr) } :text
  .rel.dyn : { *(.rel.*) } :text
  . = ALIGN(8);
  __text_end = .;

  . = 0x10000000;
  __data_start = .;
  .preinit_array : { KEEP (*(.preinit_array)) } :data
  .init_array : {
    KEEP (*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
    KEEP (*(.init_array .ctors))
  } :data
  .fini_array : {
    KEEP (*(SORT_BY_INIT_PRIORITY(.fini_array.*) SORT_BY_INIT_PRIORITY(.dtors.*)))
    KEEP (*(.fini_array .dtors))
  } :data
  .data.rel.ro : {
    *(.data.rel.ro.local* .gnu.linkonce.d.rel.ro.local.*)
    *(.data.rel.ro .data.rel.ro.* .gnu.linkonce.d.rel.ro.*)
  } :data
  .dynamic : { *(.dynamic) } :data :dynamic
  .got : { *(.got.plt) *(.igot.plt) *(.got) *(.igot) } :data
  .data : { *(.data .data.* .gnu.linkonce.d.*) } :data
  . = ALIGN(8);
  .bss : {
    __bss_start = .;
    *(.dynbss)
    *(.bss .bss.* .gnu.linkonce.b.*)
    *(COMMON)
    . = ALIGN(8);
    __bss_end = .;
  } :data
  _end = .;

  /DISCARD/ : { *(.interp) *(.note.GNU-stack) *(.gnu_debuglink) }
}
