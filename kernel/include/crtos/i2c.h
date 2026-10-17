/*
 * crtos/i2c.h - I2C bus.
 *
 * A controller driver registers an adapter for its device; the framework then creates a
 * device for every child node of the controller ("reg" = 7-bit address). Drivers of those
 * devices get their client with i2c_client_get(dev). Transfers on one adapter are serialised.
 */
#ifndef CRTOS_I2C_H
#define CRTOS_I2C_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct device;
struct i2c_adapter;

#define I2C_M_RD 0x0001u

struct i2c_msg {
    uint16_t addr;
    uint16_t flags;     /* I2C_M_RD */
    uint16_t len;
    uint8_t *buf;
};

struct i2c_adapter_ops {
    int (*xfer)(void *ctx, struct i2c_msg *msgs, int num);   /* number of messages done or -errno */
};

struct i2c_client {
    struct i2c_adapter *adapter;
    uint16_t addr;
    struct device *dev;
};

int i2c_adapter_register(struct device *dev, const struct i2c_adapter_ops *ops, void *ctx, struct i2c_adapter **out);
void i2c_adapter_unregister(struct i2c_adapter *adap);
struct i2c_adapter *i2c_adapter_get(int bus);          /* by registration order, for tools */

struct i2c_client *i2c_client_get(struct device *dev);
int i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num);
int i2c_write(struct i2c_client *c, const void *buf, size_t len);
int i2c_read(struct i2c_client *c, void *buf, size_t len);
int i2c_write_read(struct i2c_client *c, const void *wbuf, size_t wlen, void *rbuf, size_t rlen);

#ifdef __cplusplus
}
#endif

#endif
