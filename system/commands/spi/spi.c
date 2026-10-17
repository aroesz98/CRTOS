/*
 * spi - one SPI message through /dev/spidev*: sends the bytes given in hex and shows what
 * came back (as Linux spidev_test).
 *
 *     spi [-D dev] [-s hz] [-m 0..3] [-l] [-c] [-r n] [HEX...]
 *
 *   -D   the device (default /dev/spidev3.0)
 *   -s   clock in Hz (default: the device's)
 *   -m   SPI mode 0-3 (clock polarity and phase)
 *   -l   loopback check: received must equal sent (wire MOSI to MISO: on the EVKB J24 pin 9
 *        to the camera connector J35 pin 3 - not to J24 pin 2, the display's enable line)
 *   -r   read n more bytes (zeros are sent for them)
 *   -c   command, then read: the HEX bytes are only sent, then the -r bytes only read, as two
 *        parts of one message (the chip select stays active), e.g. "spi -c -r 3 9f" reads the
 *        ID of an SPI flash
 *
 * At most 16 KB in all; of a long answer only the start is shown.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <crtos.h>
#include <crtos/spi.h>

#define MAX 16384 /* the most /dev/spidev takes in one message */
#define SHOW 256

static void usage(void)
{
    printf("usage: spi [-D dev] [-s hz] [-m 0..3] [-l] [-c] [-r n] [HEX...]\n");
}

int main(int argc, char **argv)
{
    const char *dev = "/dev/spidev3.0";
    uint32_t speed = 0, extra = 0;
    int mode = -1, loop = 0, cmd = 0, i = 1;
    static uint8_t tx[MAX], rx[MAX];
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        char o = argv[i][1];
        if (o == 'l' || o == 'c') {
            *(o == 'l' ? &loop : &cmd) = 1;
            continue;
        }
        if (i + 1 >= argc || (o != 'D' && o != 's' && o != 'm' && o != 'r')) {
            usage();
            return 2;
        }
        const char *v = argv[++i];
        if (o == 'D')
            dev = v;
        else if (o == 's')
            speed = (uint32_t)strtoul(v, NULL, 0);
        else if (o == 'm')
            mode = atoi(v) & 3;
        else
            extra = (uint32_t)strtoul(v, NULL, 0);
    }
    uint32_t n = 0;
    for (; i < argc && n < MAX; i++) {
        char *end;
        unsigned long b = strtoul(argv[i], &end, 16);
        if (*end || b > 0xFF) {
            printf("spi: '%s' is not a hex byte\n", argv[i]);
            return 2;
        }
        tx[n++] = (uint8_t)b;
    }
    if (extra > MAX - n)
        extra = MAX - n;
    uint32_t ncmd = n;
    n += extra; /* (zeros) */
    if (!n || (cmd && (!ncmd || !extra || loop))) {
        usage();
        return 2;
    }
    int fd = open(dev, O_RDWR);
    if (fd < 0) {
        printf("spi: %s: %s\n", dev, strerror(errno));
        return 1;
    }
    uint32_t m32 = 0;
    ioctl(fd, SPI_IOC_RD_MODE32, &m32);
    if (mode >= 0)
        m32 = (m32 & ~(uint32_t)SPI_MODE_3) | (uint32_t)mode;
    m32 &= ~SPI_LOOP;
    if (ioctl(fd, SPI_IOC_WR_MODE32, &m32) < 0 || (speed && ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0)) {
        printf("spi: settings: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    struct spi_ioc_transfer t[2];
    memset(t, 0, sizeof(t));
    t[0].tx_buf = (uintptr_t)tx;
    t[0].rx_buf = (uintptr_t)rx;
    t[0].len = n;
    if (cmd) { /* the command only sent, the answer only read */
        t[0].rx_buf = 0;
        t[0].len = ncmd;
        t[1].rx_buf = (uintptr_t)rx;
        t[1].len = extra;
    }
    uint64_t t0 = crtos_time_us();
    int r = cmd ? ioctl(fd, SPI_IOC_MESSAGE(2), t) : ioctl(fd, SPI_IOC_MESSAGE(1), t);
    uint64_t us = crtos_time_us() - t0;
    uint32_t hz = 0;
    ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &hz);
    close(fd);
    if (r < 0) {
        printf("spi: transfer: %s\n", strerror(errno));
        return 1;
    }
    uint32_t nrx = cmd ? extra : n;
    printf("%lu byte(s) at %lu Hz, mode %lu, in %lu us\nreceived:", (unsigned long)n, (unsigned long)hz,
           (unsigned long)(m32 & 3u), (unsigned long)us);
    for (uint32_t k = 0; k < nrx && k < SHOW; k++)
        printf("%s%02x", k % 16 ? " " : "\n  ", rx[k]);
    if (nrx > SHOW)
        printf("\n  ... %lu more", (unsigned long)(nrx - SHOW));
    printf("\n");
    if (loop) {
        bool same = !memcmp(tx, rx, n);
        printf("loopback: %s\n", same ? "received = sent" : "DIFFERENT");
        return same ? 0 : 1;
    }
    return 0;
}
