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

#include <stdio.h>
#include "i2c-at24.h"

#define LOG1    //printf
#define LOG2    //printf

void i2c_at24_memcpy(void *dst, const void *src, unsigned int offset, size_t len, size_t max_len)
{
    size_t cpy, fill;

    if (max_len <= offset) {
        cpy = 0;
        fill = len;
    } else if (max_len >= offset + len) {
        cpy = len;
        fill = 0;
    } else {
        cpy = max_len - offset;
        fill = len - cpy;
    }

    memcpy(dst, src + offset, cpy);
    memset(dst + cpy, 0xff, fill);
}

bool i2c_at24_read(const struct dln2_i2c_device *dev, uint16_t address, void *buf, size_t len)
{
    struct i2c_at24_device *at24 = (struct i2c_at24_device *)dev;
    unsigned int offset = at24->data->offset;
    size_t cpy, fill;

    LOG1("0x%02x: AT24 READ %zu@%u\n", address, len, offset);

    if (!len)
        return true;

    if (offset + len > at24->size) {
        LOG1("AT24 READ WRAP AROUND NOT IMPL.\n");
        return false;
    }

    int ret = i2c_at24_flash_read(at24, address, offset, buf, len);
    if (ret < 0)
        return false;
    at24->data->offset += len;
    if (ret > 0)
        return true;

    TU_ASSERT(at24->initial_data && at24->initial_data_size);
    i2c_at24_memcpy(buf, at24->initial_data, offset, len, at24->initial_data_size);

    return true;
}

bool i2c_at24_write(const struct dln2_i2c_device *dev, uint16_t address, const void *buf, size_t len)
{
    struct i2c_at24_device *at24 = (struct i2c_at24_device *)dev;
    unsigned int offset;

    LOG2("%s: address=0x%02x len=%zu\n", __func__, address, len);

    if (len < at24->addr_size) {
        LOG1("AT24 SHORT WRITE len=%zu\n", len);
        return false;
    }

    if (at24->addr_size == 1)
        offset = *(uint8_t *)buf;
    else if (at24->addr_size == 2)
        offset = get_unaligned_be16(buf);
    else
        return false;

    if (offset >= at24->size) {
        LOG1("0x%02x: AT24 OFFSET=%u TOO LARGE\n", address, offset);
        return false;
    }

    at24->data->offset = offset;

    if (len == at24->addr_size) {
        LOG2("0x%02x: AT24 WRITE OFFSET %u\n", address, offset);
        return true;
    }

    if (at24->readonly)
        return false;

    buf += at24->addr_size;
    len -= at24->addr_size;

    if (offset + len > at24->size) {
        LOG1("AT24 write WRAP AROUND NOT IMPL.\n");
        return false;
    }

    LOG1("0x%02x: AT24 WRITE %zu@%u\n", address, len, offset);

    return i2c_at24_flash_write(at24, address, offset, buf, len);
}
