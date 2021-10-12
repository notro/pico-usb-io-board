// SPDX-License-Identifier: CC0-1.0

#ifndef _I2C_AT24_H_
#define _I2C_AT24_H_

#include "dln2-devices.h"

struct i2c_at24_device_data {
    unsigned int offset;
};

struct i2c_at24_device {
    struct dln2_i2c_device base;
    struct i2c_at24_device_data *data;
    size_t size;
    size_t addr_size;
    const void *initial_data;
    size_t initial_data_size;
    bool readonly;
};

#define DEFINE_I2C_AT24(_var, _addr, _idata, _isize, _name, _size, _addr_size)  \
    struct i2c_at24_device_data _var##_data;                    \
    struct i2c_at24_device (_var) = {                           \
        .base = {                                               \
            .name = _name,                                      \
            .address = (_addr),                                 \
            .read = i2c_at24_read,                              \
            .write = i2c_at24_write,                            \
        },                                                      \
        .data = &(_var##_data),                                 \
        .size = (_size),                                        \
        .addr_size = (_addr_size),                              \
        .initial_data = _idata,                                 \
        .initial_data_size = _isize,                            \
    }

#define DEFINE_I2C_AT24C32(_var, _addr, _idata, _isize)     DEFINE_I2C_AT24(_var, _addr, _idata, _isize, "24c32", 4 * 1024, 2)
//#define DEFINE_I2C_AT24C256(_var, _addr)    DEFINE_I2C_AT24(_var, _addr, "24c256", 32 * 1024, 2)

bool i2c_at24_read(const struct dln2_i2c_device *dev, uint16_t address, void *buf, size_t len);
bool i2c_at24_write(const struct dln2_i2c_device *dev, uint16_t address, const void *buf, size_t len);
void i2c_at24_memcpy(void *dst, const void *src, unsigned int offset, size_t len, size_t max_len);

int i2c_at24_flash_read(const struct i2c_at24_device *at24, uint16_t address, unsigned int offset, void *buf, size_t len);
bool i2c_at24_flash_write(const struct i2c_at24_device *at24, uint16_t address, unsigned int offset, const void *buf, size_t len);

#endif
