/*
 * memory.ld - memory map of the EVKB-IMXRT1050 as the kernel uses it (ITCM from 0x100:
 * the first 256 bytes stay unmapped as the NULL guard)
 *
 * Derived from the linker script MCUXpresso IDE v25.6 generates for MIMXRT1052xxxxB
 * (Copyright 2008-2013 Code Red Technologies Ltd, Copyright 2013-2026 NXP,
 * SPDX-License-Identifier: BSD-3-Clause), kept here so that the kernel builds without the IDE.
 */

MEMORY
{
  /* Define each memory region */
  BOARD_FLASH (rx) : ORIGIN = 0x60000000, LENGTH = 0x4000000 /* 64M bytes (alias Flash) */  
  SRAM_DTC (rwx) : ORIGIN = 0x20000000, LENGTH = 0x20000 /* 128K bytes (alias RAM) */  
  SRAM_ITC (rwx) : ORIGIN = 0x100, LENGTH = 0x1ff00 /* 130816 bytes (alias RAM2) */  
  SRAM_OC (rwx) : ORIGIN = 0x20200000, LENGTH = 0x40000 /* 256K bytes (alias RAM3) */  
  BOARD_SDRAM (rwx) : ORIGIN = 0x80000000, LENGTH = 0x1e00000 /* 30M bytes (alias RAM4) */  
  NCACHE_REGION (rwx) : ORIGIN = 0x81e00000, LENGTH = 0x200000 /* 2M bytes (alias RAM5) */  
}

  /* Define a symbol for the top of each memory region */
  __base_BOARD_FLASH = 0x60000000  ; /* BOARD_FLASH */  
  __base_Flash = 0x60000000 ; /* Flash */  
  __top_BOARD_FLASH = 0x60000000 + 0x4000000 ; /* 64M bytes */  
  __top_Flash = 0x60000000 + 0x4000000 ; /* 64M bytes */  
  __base_SRAM_DTC = 0x20000000  ; /* SRAM_DTC */  
  __base_RAM = 0x20000000 ; /* RAM */  
  __top_SRAM_DTC = 0x20000000 + 0x20000 ; /* 128K bytes */  
  __top_RAM = 0x20000000 + 0x20000 ; /* 128K bytes */  
  __base_SRAM_ITC = 0x100  ; /* SRAM_ITC */  
  __base_RAM2 = 0x100 ; /* RAM2 */  
  __top_SRAM_ITC = 0x100 + 0x1ff00 ; /* 130816 bytes */  
  __top_RAM2 = 0x100 + 0x1ff00 ; /* 130816 bytes */  
  __base_SRAM_OC = 0x20200000  ; /* SRAM_OC */  
  __base_RAM3 = 0x20200000 ; /* RAM3 */  
  __top_SRAM_OC = 0x20200000 + 0x40000 ; /* 256K bytes */  
  __top_RAM3 = 0x20200000 + 0x40000 ; /* 256K bytes */  
  __base_BOARD_SDRAM = 0x80000000  ; /* BOARD_SDRAM */  
  __base_RAM4 = 0x80000000 ; /* RAM4 */  
  __top_BOARD_SDRAM = 0x80000000 + 0x1e00000 ; /* 30M bytes */  
  __top_RAM4 = 0x80000000 + 0x1e00000 ; /* 30M bytes */  
  __base_NCACHE_REGION = 0x81e00000  ; /* NCACHE_REGION */  
  __base_RAM5 = 0x81e00000 ; /* RAM5 */  
  __top_NCACHE_REGION = 0x81e00000 + 0x200000 ; /* 2M bytes */  
  __top_RAM5 = 0x81e00000 + 0x200000 ; /* 2M bytes */  
