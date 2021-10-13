// SPDX-License-Identifier: CC0-1.0
/*
 * Written in 2021 by Noralf Trønnes <noralf@tronnes.org>
 *
 * To the extent possible under law, the author(s) have dedicated all copyright and related and
 * neighboring rights to this software to the public domain worldwide. This software is
 * distributed without any warranty.
 *
 * You should have received a copy of the CC0 Public Domain Dedication along with this software.
 * If not, see <http://creativecommons.org/publicdomain/zero/1.0/>.
 */

#ifndef _DLN2_DEVICES_H_
#define _DLN2_DEVICES_H_

#include "dln2.h"

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
