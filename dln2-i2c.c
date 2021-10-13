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
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "dln2.h"
#include "dln2-devices.h"

#define LOG1    //printf
#define LOG2    //printf

static struct dln2_i2c_device **dln2_i2c_devices;

#define DLN2_I2C_CMD(cmd)       DLN2_CMD(cmd, DLN2_MODULE_I2C)

#define DLN2_I2C_ENABLE                 DLN2_I2C_CMD(0x01)
#define DLN2_I2C_DISABLE                DLN2_I2C_CMD(0x02)
#define DLN2_I2C_WRITE                  DLN2_I2C_CMD(0x06)
#define DLN2_I2C_READ                   DLN2_I2C_CMD(0x07)

// Linux driver timeout is 200ms
#define DLN2_I2C_TIMEOUT_US     (150 * 1000)

static bool dln2_i2c_enable(struct dln2_slot *slot, bool enable)
{
    uint8_t *port = dln2_slot_header_data(slot);
    uint scl = PICO_DEFAULT_I2C_SCL_PIN;
    uint sda = PICO_DEFAULT_I2C_SDA_PIN;
    int res;

    LOG1("    %s: port=%u enable=%u\n", __func__, *port, enable);

    if (dln2_slot_header_data_size(slot) != sizeof(*port))
        return dln2_response_error(slot, DLN2_RES_INVALID_COMMAND_SIZE);
    if (*port)
        return dln2_response_error(slot, DLN2_RES_INVALID_PORT_NUMBER);

    if (enable) {
        res = dln2_pin_request(scl, DLN2_MODULE_I2C);
        if (res)
            return dln2_response_error(slot, res);

        res = dln2_pin_request(sda, DLN2_MODULE_I2C);
        if (res) {
            dln2_pin_free(scl, DLN2_MODULE_I2C);
            return dln2_response_error(slot, res);
        }

        i2c_init(i2c_default, 100 * 1000);
        gpio_set_function(scl, GPIO_FUNC_I2C);
        gpio_set_function(sda, GPIO_FUNC_I2C);
    } else {
        res = dln2_pin_free(sda, DLN2_MODULE_I2C);
        if (res)
            return dln2_response_error(slot, res);

        res = dln2_pin_free(scl, DLN2_MODULE_I2C);
        if (res)
            return dln2_response_error(slot, res);

        gpio_set_function(sda, GPIO_FUNC_NULL);
        gpio_set_function(scl, GPIO_FUNC_NULL);
        i2c_deinit(i2c_default);
    }

    return dln2_response(slot, 0);
}

struct dln2_i2c_read_msg_tx {
    uint8_t port;
    uint8_t addr;
    uint8_t mem_addr_len;
    uint32_t mem_addr;
    uint16_t buf_len;
} TU_ATTR_PACKED;

static bool dln2_i2c_read(struct dln2_slot *slot)
{
    struct dln2_i2c_read_msg_tx *msg = dln2_slot_header_data(slot);
    uint8_t *rx = dln2_slot_response_data(slot);
    size_t len = msg->buf_len;

    LOG1("    %s: port=%u addr=0x%02x buf_len=%u\n", __func__, msg->port, msg->addr, msg->buf_len);

    if (dln2_slot_header_data_size(slot) != sizeof(*msg))
        return dln2_response_error(slot, DLN2_RES_INVALID_COMMAND_SIZE);
    if (msg->port)
        return dln2_response_error(slot, DLN2_RES_INVALID_PORT_NUMBER);

    if (!dln2_i2c_devices)
        return dln2_response_error(slot, DLN2_RES_I2C_MASTER_SENDING_ADDRESS_FAILED);

    for (struct dln2_i2c_device **ptr = dln2_i2c_devices; *ptr; ptr++) {
        struct dln2_i2c_device *dev = *ptr;
        if (dev->address && dev->address != msg->addr)
            continue;

        if (!dev->read(dev, msg->addr, rx + 2, len))
            break;
        put_unaligned_le16(len, rx);
        return dln2_response(slot, len + 2);
    }

    int ret = i2c_read_timeout_us(i2c_default, msg->addr, rx + 2, len, false, DLN2_I2C_TIMEOUT_US);
    if (ret < 0)
        return dln2_response_error(slot, DLN2_RES_I2C_MASTER_SENDING_ADDRESS_FAILED);
    // The linux driver returns -EPROTO if length differs, so use a descriptive error (there was no read error code)
    if (ret != len)
        return dln2_response_error(slot, DLN2_RES_I2C_MASTER_SENDING_DATA_FAILED);

    put_unaligned_le16(len, rx);
    return dln2_response(slot, len + 2);
}

struct dln2_i2c_write_msg {
    uint8_t port;
    uint8_t addr;
    uint8_t mem_addr_len;
    uint32_t mem_addr;
    uint16_t buf_len;
    uint8_t buf[];
} TU_ATTR_PACKED;

static bool dln2_i2c_write(struct dln2_slot *slot)
{
    struct dln2_i2c_write_msg *msg = dln2_slot_header_data(slot);

    LOG1("    %s: port=%u addr=0x%02x buf_len=%u\n", __func__, msg->port, msg->addr, msg->buf_len);

    if (dln2_slot_header_data_size(slot) < sizeof(*msg))
        return dln2_response_error(slot, DLN2_RES_INVALID_COMMAND_SIZE);
    if (msg->port)
        return dln2_response_error(slot, DLN2_RES_INVALID_PORT_NUMBER);

    if (!dln2_i2c_devices)
        return dln2_response_error(slot, DLN2_RES_I2C_MASTER_SENDING_ADDRESS_FAILED);

    for (struct dln2_i2c_device **ptr = dln2_i2c_devices; *ptr; ptr++) {
        struct dln2_i2c_device *dev = *ptr;
        if (dev->address && dev->address != msg->addr)
            continue;

        if (!dev->write(dev, msg->addr, msg->buf, msg->buf_len))
            break;
        return dln2_response(slot, msg->buf_len);
    }

    int ret = i2c_write_timeout_us(i2c_default, msg->addr, msg->buf, msg->buf_len, false, DLN2_I2C_TIMEOUT_US);
    LOG2("        i2c_write_timeout_us: ret =%d\n", ret);
    if (ret < 0)
        return dln2_response_error(slot, DLN2_RES_I2C_MASTER_SENDING_ADDRESS_FAILED);
    if (ret != msg->buf_len)
        return dln2_response_error(slot, DLN2_RES_I2C_MASTER_SENDING_DATA_FAILED);

    return dln2_response(slot, msg->buf_len);
}

bool dln2_handle_i2c(struct dln2_slot *slot)
{
    struct dln2_header *hdr = dln2_slot_header(slot);

    switch (hdr->id) {
    case DLN2_I2C_ENABLE:
        return dln2_i2c_enable(slot, true);
    case DLN2_I2C_DISABLE:
        return dln2_i2c_enable(slot, false);
    case DLN2_I2C_WRITE:
        return dln2_i2c_write(slot);
    case DLN2_I2C_READ:
        return dln2_i2c_read(slot);
    default:
        LOG1("I2C: unknown command 0x%02x\n", hdr->id);
        return dln2_response_error(slot, DLN2_RES_COMMAND_NOT_SUPPORTED);
    }
}

void dln2_i2c_set_devices(struct dln2_i2c_device **devs)
{
    dln2_i2c_devices = devs;
}
