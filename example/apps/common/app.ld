/*
 * app.ld - CRTOS Application Linker Script
 * Author: Arkadiusz Szlanta
 * Date: 05 Jan 2026
 *
 * Description:
 * Linker script for CRTOS user applications.
 * Applications are loaded to RAM and relocated by AppLoader.
 * Uses PIC (Position Independent Code) with GOT.
 */

/* No fixed memory regions - AppLoader allocates memory dynamically */
/* Base address is 0, AppLoader will relocate all addresses */

ENTRY(app_main)

SECTIONS
{
    /* Start at address 0 - AppLoader will relocate */
    . = 0;
    
    /* Application header with metadata */
    .app_header :
    {
        /* Magic number for validation */
        LONG(0x43525453)        /* "CRTS" */
        LONG(0x41505000)        /* "APP\0" */
        
        /* Entry point offset */
        LONG(app_main)
        
        /* Section info (filled by linker) */
        LONG(__text_start)
        LONG(__text_size)
        LONG(__rodata_start)
        LONG(__rodata_size)
        LONG(__data_start)
        LONG(__data_size)
        LONG(__bss_start)
        LONG(__bss_size)
        LONG(__got_start)
        LONG(__got_size)
        
        /* Reserved */
        LONG(0)
        LONG(0)
    }
    
    /* Code section */
    .text ALIGN(4) :
    {
        __text_start = .;
        *(.text)
        *(.text.*)
        *(.glue_7)
        *(.glue_7t)
        *(.eh_frame)
        . = ALIGN(4);
        __text_end = .;
    }
    __text_size = __text_end - __text_start;
    
    /* Read-only data */
    .rodata ALIGN(4) :
    {
        __rodata_start = .;
        *(.rodata)
        *(.rodata.*)
        . = ALIGN(4);
        __rodata_end = .;
    }
    __rodata_size = __rodata_end - __rodata_start;
    
    /* ARM exception handling */
    .ARM.extab ALIGN(4) :
    {
        *(.ARM.extab*)
    }
    
    .ARM.exidx ALIGN(4) :
    {
        *(.ARM.exidx*)
    }
    
    /* C++ constructors */
    .init_array ALIGN(4) :
    {
        __init_array_start = .;
        KEEP(*(.init_array))
        KEEP(*(.init_array.*))
        __init_array_end = .;
    }
    
    /* C++ destructors */
    .fini_array ALIGN(4) :
    {
        __fini_array_start = .;
        KEEP(*(.fini_array))
        KEEP(*(.fini_array.*))
        __fini_array_end = .;
    }
    
    /* Global Offset Table for PIC */
    .got ALIGN(4) :
    {
        __got_start = .;
        *(.got)
        *(.got.plt)
        . = ALIGN(4);
        __got_end = .;
    }
    __got_size = __got_end - __got_start;
    
    /* Read-only data that needs relocation (vtables, etc.) */
    .data.rel.ro ALIGN(4) :
    {
        *(.data.rel.ro)
        *(.data.rel.ro.*)
    }
    
    /* Initialized data */
    .data ALIGN(4) :
    {
        __data_start = .;
        *(.data)
        *(.data.*)
        . = ALIGN(4);
        __data_end = .;
    }
    __data_size = __data_end - __data_start;
    
    /* Uninitialized data (zeroed by loader) */
    .bss ALIGN(4) (NOLOAD) :
    {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    }
    __bss_size = __bss_end - __bss_start;
    
    /* End of application image */
    . = ALIGN(4);
    __app_end = .;
    __app_size = __app_end;
    
    /* Discard sections we don't need */
    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.debug*)
    }
}

/* Provide symbols for AppLoader */
PROVIDE(__app_entry = app_main);
