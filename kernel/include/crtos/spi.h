/*
 * crtos/spi.h - SPI bus.
 *
 * A controller driver registers its bus; the framework then creates a device for every
 * child node of the controller in the device tree ("reg" = chip select, "spi-max-frequency",
 * "spi-cpol", "spi-cpha", "spi-cs-high", "spi-lsb-first"). Drivers of those devices get
 * theirs with spi_device_get(dev) and talk with spi_sync(). A child with the compatible
 * "crtos,spidev" is for programs instead: /dev/spidev<bus>.<cs>, with the ioctls below (as
 * Linux spidev). Messages on one bus are serialised.
 */
#ifndef CRTOS_SPI_H
#define CRTOS_SPI_H

#include <stdint.h>
#include <crtos/ioctl.h>

/* mode bits (as in Linux) */
#define SPI_CPHA        0x01u   /* sample on the second clock edge */
#define SPI_CPOL        0x02u   /* clock idles high */
#define SPI_MODE_0      0u
#define SPI_MODE_1      SPI_CPHA
#define SPI_MODE_2      SPI_CPOL
#define SPI_MODE_3      (SPI_CPOL | SPI_CPHA)
#define SPI_CS_HIGH     0x04u   /* chip select active high */
#define SPI_LSB_FIRST   0x08u
#define SPI_LOOP        0x20u   /* the controller reads back what it sends (if it can) */

/* ---- programs: /dev/spidevB.C ---- */

/* One part of a message; the chip select stays active from the first part to the last
 * unless cs_change (then it goes inactive after this part). */
struct spi_ioc_transfer {
    uint64_t tx_buf;            /* address of the bytes to send, 0: send zeros */
    uint64_t rx_buf;            /* where received bytes go, 0: dropped */
    uint32_t len;
    uint32_t speed_hz;          /* 0: the device's */
    uint16_t delay_usecs;       /* after this part */
    uint8_t bits_per_word;      /* 0: the device's (8) */
    uint8_t cs_change;
    uint8_t tx_nbits, rx_nbits; /* (single line only) */
    uint8_t word_delay_usecs;
    uint8_t pad;
};

#define SPI_IOC_MAGIC 'k'
#define SPI_MSGSIZE(n) ((n) * sizeof(struct spi_ioc_transfer))
#define SPI_IOC_MESSAGE(n) _IOC(_IOC_WRITE, SPI_IOC_MAGIC, 0, SPI_MSGSIZE(n))
#define SPI_IOC_RD_MODE             _IOR(SPI_IOC_MAGIC, 1, uint8_t)
#define SPI_IOC_WR_MODE             _IOW(SPI_IOC_MAGIC, 1, uint8_t)
#define SPI_IOC_RD_LSB_FIRST        _IOR(SPI_IOC_MAGIC, 2, uint8_t)
#define SPI_IOC_WR_LSB_FIRST        _IOW(SPI_IOC_MAGIC, 2, uint8_t)
#define SPI_IOC_RD_BITS_PER_WORD    _IOR(SPI_IOC_MAGIC, 3, uint8_t)
#define SPI_IOC_WR_BITS_PER_WORD    _IOW(SPI_IOC_MAGIC, 3, uint8_t)
#define SPI_IOC_RD_MAX_SPEED_HZ     _IOR(SPI_IOC_MAGIC, 4, uint32_t)
#define SPI_IOC_WR_MAX_SPEED_HZ     _IOW(SPI_IOC_MAGIC, 4, uint32_t)
#define SPI_IOC_RD_MODE32           _IOR(SPI_IOC_MAGIC, 5, uint32_t)
#define SPI_IOC_WR_MODE32           _IOW(SPI_IOC_MAGIC, 5, uint32_t)

/* ---- the kernel ---- */
#if !defined(__ASSEMBLER__) && !defined(CRTOS_USER)
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct device;
struct spi_controller;

struct spi_device {
    struct spi_controller *ctlr;
    struct device *dev;         /* the device of the child node (NULL for spidev) */
    uint8_t cs;
    uint8_t bits_per_word;      /* 8 by default */
    uint32_t mode;              /* SPI_* */
    uint32_t max_speed_hz;
};

/* Kernel form of a part of a message */
struct spi_transfer {
    const void *tx_buf;         /* NULL: send zeros */
    void *rx_buf;               /* NULL: drop */
    uint32_t len;
    uint32_t speed_hz;          /* 0: the device's */
    uint16_t delay_us;
    uint8_t bits_per_word;      /* 0: the device's */
    uint8_t cs_change;
};

struct spi_controller_ops {
    /* One message to @spi: its parts in order, the chip select as struct spi_ioc_transfer
     * describes. 0 or -errno. */
    int (*transfer)(void *ctx, struct spi_device *spi, struct spi_transfer *xfers, int num);
};

int spi_controller_register(struct device *dev, const struct spi_controller_ops *ops, void *ctx, int num_cs,
                            struct spi_controller **out);
void spi_controller_unregister(struct spi_controller *c);
int spi_controller_bus(struct spi_controller *c);

struct spi_device *spi_device_get(struct device *dev);
int spi_sync(struct spi_device *spi, struct spi_transfer *xfers, int num);
int spi_write(struct spi_device *spi, const void *buf, size_t len);
int spi_read(struct spi_device *spi, void *buf, size_t len);
int spi_write_then_read(struct spi_device *spi, const void *tx, size_t ntx, void *rx, size_t nrx);

#ifdef __cplusplus
}
#endif
#endif

#endif
