// SPDX-License-Identifier: CC0-1.0

#ifndef _DLN2_DEVICES_H_
#define _DLN2_DEVICES_H_

//#include <stdbool.h>
//#include <stdint.h>
//#include "common/tusb_common.h"

#include "dln2.h"


//#define container_of(ptr, type, member) ({ \
//            const typeof( ((type *)0)->member ) *__mptr = (ptr);
//            (type *)( (char *)__mptr - offsetof(type,member) );})


struct dln2_i2c_device {
    const char *name;
    uint16_t address;

    // @buf is unaligned
    bool (*read)(const struct dln2_i2c_device *dev, uint16_t address, void *buf, size_t len);
    // @buf is aligned
    bool (*write)(const struct dln2_i2c_device *dev, uint16_t address, const void *buf, size_t len);

};

void dln2_i2c_set_devices(struct dln2_i2c_device **devs);

#endif
