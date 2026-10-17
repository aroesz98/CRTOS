/*
 * kernel/os/boot/sdcard.h - built-in SD card driver (USDHC1), needed to load everything else.
 */
#ifndef CRTOS_BOOT_SDCARD_H
#define CRTOS_BOOT_SDCARD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SDCARD_BLOCK_SIZE 512u

struct sdcard_info {
    int present;
    int ready;
    int high_capacity;      /* SDHC/SDXC (block addressed) */
    uint32_t blocks;        /* 512-byte blocks */
    uint32_t clock_hz;
    int bus_width;
    uint32_t cid[4];
    uint32_t reads, writes;         /* blocks */
    uint32_t errors;                /* failed transfers */
    uint32_t retries, reinits;      /* recovery actions */
    uint32_t slow, max_us;          /* transfers slower than 250 ms, slowest transfer */
    int mode;                       /* bus mode: 0 default speed, 1 high speed, 2 SDR50, 3 SDR104 */
    int v18;                        /* 1.8 V signalling */
    uint32_t tunings;               /* sampling point searches (tuning) */
};

int sdcard_init(void);                       /* pins, clocks, host; initialises a card if present */
int sdcard_present(void);
int sdcard_read(uint32_t lba, void *buf, uint32_t count);
int sdcard_write(uint32_t lba, const void *buf, uint32_t count);
void sdcard_get_info(struct sdcard_info *info);
void sdcard_timing(uint32_t *cmd_us, uint32_t *data_us, uint32_t *total_us, uint32_t *count);

/* diagnostics (kmon) */
int sdcard_reinit(void);                     /* reset the controller and initialise the card again */
uint32_t sdcard_set_clock(uint32_t hz);      /* limit the bus clock (re-initialises the card) */
int sdcard_set_mode(int mode);               /* the fastest bus mode to use (re-initialises the card) */
const char *sdcard_mode_name(int mode);
int sdcard_set_cmd23(int on);                /* use CMD23 (if the card has it); the setting before */
struct file;
int fat_file_extent(struct file *f, uint32_t *lba, uint32_t *blocks);   /* os/boot/fat.cpp, tests */
void sdcard_set_pads(uint32_t speed, uint32_t dse, uint32_t fast_slew);
void sdcard_dump_trace(int (*pr)(const char *fmt, ...));  /* recent transfers */
void sdcard_dump_hist(int (*pr)(const char *fmt, ...));   /* block commands by size since the last call */

#ifdef __cplusplus
}
#endif

#endif
