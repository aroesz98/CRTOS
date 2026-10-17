/*
 * memory.ld - the memory map of the STM32H7S78-DK as the kernel uses it. The region names are
 * the ones of the i.MX RT board (kmalloc and the kernel's section attributes use them):
 *
 *   BOARD_FLASH    the "kernel" partition of the NOR flash (XSPI2, memory-mapped by the boot loader)
 *   SRAM_ITC       ITCM up to 192 KB from 0x100 (the first 256 bytes stay unmapped as the NULL
 *                  guard): the option bytes give it AXI SRAM1, 64 KB by default, 128 KB
 *                  recommended for CRTOS (ITCM_AXI_SHARE=1; the upper half of SRAM1 is the
 *                  GPU's). The kernel's code takes the first ~56 KB (crtos.ld checks that 64 KB
 *                  still leave fast code); the fast code area of programs ends where the ITCM
 *                  does (platform_itcm_top)
 *   SRAM_DTC       DTCM 64 KB (the option bytes' default)
 *   SRAM_OC        AXI SRAM2-4 328 KB from 0x24020000 (ECC off: SRAM4 is 72 KB), there with any
 *                  ITCM split - the on-chip RAM the bus masters (GPU, LTDC, SDMMC, Ethernet) reach
 *   BOARD_SDRAM    PSRAM (XSPI1) 30 MB, cached
 *   NCACHE_REGION  its last 2 MB, not cached: frame buffers
 */

MEMORY
{
  BOARD_FLASH (rx) : ORIGIN = 0x70000000, LENGTH = 0x200000
  SRAM_DTC (rwx) : ORIGIN = 0x20000000, LENGTH = 0x10000
  SRAM_ITC (rwx) : ORIGIN = 0x100, LENGTH = 0x2ff00
  SRAM_OC (rwx) : ORIGIN = 0x24020000, LENGTH = 0x52000
  BOARD_SDRAM (rwx) : ORIGIN = 0x90000000, LENGTH = 0x1e00000
  NCACHE_REGION (rwx) : ORIGIN = 0x91e00000, LENGTH = 0x200000
}

  __base_BOARD_FLASH = 0x70000000 ;
  __top_BOARD_FLASH = 0x70000000 + 0x200000 ;
  __base_SRAM_DTC = 0x20000000 ;
  __top_SRAM_DTC = 0x20000000 + 0x10000 ;
  __base_SRAM_ITC = 0x100 ;
  __top_SRAM_ITC = 0x100 + 0x2ff00 ;
  __base_SRAM_OC = 0x24020000 ;
  __top_SRAM_OC = 0x24020000 + 0x52000 ;
  __base_BOARD_SDRAM = 0x90000000 ;
  __top_BOARD_SDRAM = 0x90000000 + 0x1e00000 ;
  __base_NCACHE_REGION = 0x91e00000 ;
  __top_NCACHE_REGION = 0x91e00000 + 0x200000 ;

/* the boot loader keeps its data and stack here (boot/boot.ld): what must survive a reset ends below */
  __boot_loader_ram = 0x2000c000 ;
