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

#ifndef _DLN2_H_
#define _DLN2_H_

#include <stdbool.h>
#include <stdint.h>
#include "common/tusb_common.h"

#define DLN2_MODULE_GENERIC     0x00
#define DLN2_MODULE_GPIO        0x01
#define DLN2_MODULE_SPI         0x02
#define DLN2_MODULE_I2C         0x03
#define DLN2_MODULE_ADC         0x06
#define DLN2_MODULE_UART        0x0e

enum dln2_handle {
    DLN2_HANDLE_EVENT = 0,
    DLN2_HANDLE_CTRL,
    DLN2_HANDLE_GPIO,
    DLN2_HANDLE_I2C,
    DLN2_HANDLE_SPI,
    DLN2_HANDLE_ADC,
    DLN2_HANDLES,
    DLN2_HANDLE_UNUSED = 0xffff,
};

struct dln2_header {
    uint16_t size;
    uint16_t id;
    uint16_t echo;
    uint16_t handle;
} TU_ATTR_PACKED;

struct dln2_response {
    struct dln2_header hdr;
    uint16_t result;
} TU_ATTR_PACKED;

#define DLN2_MAX_SLOTS   16
#define DLN2_BUF_SIZE    (256 + sizeof(struct dln2_response))

struct dln2_slot {
    uint8_t data[DLN2_BUF_SIZE];
    uint index;
    size_t len;
    struct dln2_slot *next;
};

struct dln2_slot_queue {
    struct dln2_slot *head;
};

static inline struct dln2_header *dln2_slot_header(struct dln2_slot *slot)
{
    return (struct dln2_header *)slot->data;
}

static inline void *dln2_slot_header_data(struct dln2_slot *slot)
{
    return slot->data + sizeof(struct dln2_header);
}

static inline size_t dln2_slot_header_data_size(struct dln2_slot *slot)
{
    return dln2_slot_header(slot)->size - sizeof(struct dln2_header);
}

static inline struct dln2_response *dln2_slot_response(struct dln2_slot *slot)
{
    return (struct dln2_response *)slot->data;
}

// Returns an unaligned address
static inline void *dln2_slot_response_data(struct dln2_slot *slot)
{
    return slot->data + sizeof(struct dln2_response);
}

static inline size_t dln2_slot_response_data_size(struct dln2_slot *slot)
{
    return dln2_slot_header(slot)->size - sizeof(struct dln2_response);
}

static inline uint16_t get_unaligned_be16(const void *p)
{
    const uint8_t *buf = p;
    return (buf[0] << 8) | buf[1];
}

static inline void put_unaligned_le16(uint16_t val, void *p)
{
    uint8_t *buf = p;
    buf[0] = val;
    buf[1] = val >> 8;
}

// http://dlnware.com/dll/Return-Code-Reference
#define DLN2_RES_SUCCESS                                    0
#define DLN2_RES_FAIL                                       0x83
#define DLN2_RES_BAD_PARAMETER                              0x85
#define DLN2_RES_INVALID_COMMAND_SIZE                       0x88
#define DLN2_RES_INVALID_MESSAGE_SIZE                       0x8a
#define DLN2_RES_INVALID_HANDLE                             0x8f
#define DLN2_RES_NOT_IMPLEMENTED                            0x91
#define DLN2_RES_COMMAND_NOT_SUPPORTED                      DLN2_RES_NOT_IMPLEMENTED
#define DLN2_RES_PIN_IN_USE                                 0xa5
#define DLN2_RES_INVALID_PORT_NUMBER                        0xa8
#define DLN2_RES_INVALID_EVENT_TYPE                         0xa9
#define DLN2_RES_PIN_NOT_CONNECTED_TO_MODULE                0xaa
#define DLN2_RES_INVALID_PIN_NUMBER                         0xab
#define DLN2_RES_INVALID_EVENT_PERIOD                       0xac
#define DLN2_RES_INVALID_BUFFER_SIZE                        0xae
#define DLN2_RES_SPI_MASTER_INVALID_SS_VALUE                0xb9
#define DLN2_RES_I2C_MASTER_SENDING_ADDRESS_FAILED          0xba
#define DLN2_RES_I2C_MASTER_SENDING_DATA_FAILED             0xbb
#define DLN2_RES_INVALID_CHANNEL_NUMBER                     0xc0
#define DLN2_RES_INVALID_MODE                               0xc7
#define DLN2_RES_INVALID_VALUE                              0xe2

#define DLN2_VERIFY_COMMAND_SIZE(_slot, _size)                                  \
    do {                                                                        \
        size_t len = dln2_slot_header_data_size(_slot);                         \
        if (len != (_size))                                                     \
            return dln2_response_error((_slot), DLN2_RES_INVALID_COMMAND_SIZE); \
    } while (0)

#define DLN2_CMD(cmd, id)       ((cmd) | ((id) << 8))

#define dln2_print_slot(slot)   _dln2_print_slot(slot, 0, __func__)
void _dln2_print_slot(struct dln2_slot *slot, uint indent, const char *caller);

struct dln2_slot *dln2_get_slot(void);
void dln2_queue_slot_in(struct dln2_slot *slot);

bool dln2_init(uint8_t rhport, uint8_t ep_out, uint8_t ep_in);
bool dln2_xfer_out(size_t len);
bool dln2_xfer_in(size_t len);

bool dln2_response(struct dln2_slot *slot, size_t len);
bool dln2_response_u8(struct dln2_slot *slot, uint8_t val);
bool dln2_response_u16(struct dln2_slot *slot, uint16_t val);
bool dln2_response_u32(struct dln2_slot *slot, uint32_t val);
bool dln2_response_error(struct dln2_slot *slot, uint16_t result);

void dln2_pin_set_available(uint32_t mask);
bool dln2_pin_is_requested(uint16_t pin, uint8_t module);
uint16_t dln2_pin_request(uint16_t pin, uint8_t module);
uint16_t dln2_pin_free(uint16_t pin, uint8_t module);

void dln2_gpio_init(void);
void dln2_gpio_task(void);
bool dln2_handle_gpio(struct dln2_slot *slot);
bool dln2_handle_i2c(struct dln2_slot *slot);
bool dln2_handle_spi(struct dln2_slot *slot);
bool dln2_handle_adc(struct dln2_slot *slot);

#endif
