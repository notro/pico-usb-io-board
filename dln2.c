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

#include "device/usbd_pvt.h"
#include "pico/unique_id.h"
#include "dln2.h"

#define LOG1    //printf
#define LOG2    //printf

#define DLN2_GENERIC_CMD(cmd)       DLN2_CMD(cmd, DLN2_MODULE_GENERIC)

#define DLN2_CMD_GET_DEVICE_VER     DLN2_GENERIC_CMD(0x30)
#define DLN2_CMD_GET_DEVICE_SN      DLN2_GENERIC_CMD(0x31)

#define DLN2_HW_ID  0x200

static uint8_t dln2_rhport;
static uint8_t dln2_ep_in;
static uint8_t dln2_ep_out;

static struct dln2_slot dln2_slots[DLN2_MAX_SLOTS];
static struct dln2_slot_queue dln2_slots_free;
static struct dln2_slot_queue dln2_response_queue;
static struct dln2_slot *dln2_slot_out;
static struct dln2_slot *dln2_slot_in;

static void dln2_slot_enqueue(struct dln2_slot_queue *queue, struct dln2_slot *slot)
{
    slot->next = NULL;

    if (!queue->head) {
        queue->head = slot;
        return;
    }

    struct dln2_slot *cursor = queue->head;
    while (cursor->next)
        cursor = cursor->next;
    cursor->next = slot;
}

static struct dln2_slot *dln2_slot_dequeue(struct dln2_slot_queue *queue)
{
    if (!queue->head)
        return NULL;

    struct dln2_slot *slot = queue->head;
    queue->head = queue->head->next;
    slot->next = NULL;
    return slot;
}

static void dln2_slots_init(void)
{
    dln2_slots_free.head = NULL;
    dln2_response_queue.head = NULL;
    dln2_slot_out = NULL;
    dln2_slot_in = NULL;

    for (uint i = 0; i < DLN2_MAX_SLOTS; i++) {
        struct dln2_slot *slot = &dln2_slots[i];
        slot->index = i;
        slot->len = 0;
        dln2_slot_header(slot)->handle = DLN2_HANDLE_UNUSED;
        dln2_slot_enqueue(&dln2_slots_free, slot);
    }
}

static const char *dln2_handle_names[] = {
    [DLN2_HANDLE_EVENT] = "EVENT",
    [DLN2_HANDLE_CTRL] = "CTRL",
    [DLN2_HANDLE_GPIO] = "GPIO",
    [DLN2_HANDLE_I2C] = "I2C",
    [DLN2_HANDLE_SPI] = "SPI",
    [DLN2_HANDLE_ADC] = "ADC",
};

void _dln2_print_slot(struct dln2_slot *slot, uint indent, const char *caller)
{
    struct dln2_header *hdr = dln2_slot_header(slot);

    const char *name = "UNKNOWN";
    if (hdr->handle < DLN2_HANDLES)
        name = dln2_handle_names[hdr->handle];
    else if (hdr->handle == DLN2_HANDLE_UNUSED)
        name = "UNUSED";

    if (indent)
        LOG1("%*s", indent, "");
    if (caller)
        LOG1("%s: ", caller);
    LOG1("[%u]: handle=%s[%u] id=%u size=%u echo=%u: len=%zu\n",
           slot->index, name, hdr->handle, hdr->id, hdr->size, hdr->echo, slot->len);
}

static struct dln2_slot *dln2_slot_print_queue(struct dln2_slot_queue *queue)
{
    for (struct dln2_slot *slot = queue->head; slot; slot = slot->next)
        _dln2_print_slot(slot, 4, NULL);
}

struct dln2_slot *dln2_get_slot(void)
{
    return dln2_slot_dequeue(&dln2_slots_free);
}

static void dln2_put_slot(struct dln2_slot *slot)
{
    dln2_print_slot(slot);
    memset(slot->data, 0, DLN2_BUF_SIZE);
    dln2_slot_header(slot)->handle = DLN2_HANDLE_UNUSED;
    slot->len = 0;
    dln2_slot_enqueue(&dln2_slots_free, slot);
}

static void dln2_queue_slot_out(void)
{
    struct dln2_slot *slot = dln2_get_slot();
    if (!slot) {
        LOG1("Run out of slots!\n");
        return;
    }

    dln2_print_slot(slot);

    bool ret = usbd_edpt_xfer(dln2_rhport, dln2_ep_out, slot->data, CFG_DLN2_BULK_ENPOINT_SIZE);
    if (!ret) {
        dln2_put_slot(slot);
        return;
    }

    dln2_slot_out = slot;
}

bool dln2_init(uint8_t rhport, uint8_t ep_out, uint8_t ep_in)
{
    dln2_rhport = rhport;
    dln2_ep_out = ep_out;
    dln2_ep_in = ep_in;

    dln2_slots_init();
    dln2_queue_slot_out();

    return true;
}

static void dln2_slot_in_xfer(void)
{
    if (dln2_slot_in)
        return;

    LOG2("%s:\n", __func__);

    struct dln2_slot *slot = dln2_slot_dequeue(&dln2_response_queue);
    if (!slot)
        return;

    struct dln2_response *response = dln2_slot_response(slot);
    bool ret = usbd_edpt_xfer(dln2_rhport, dln2_ep_in, slot->data, response->hdr.size);
    if (!ret) {
        dln2_put_slot(slot);
        return;
    }

    dln2_slot_in = slot;
}

// Host IN
void dln2_queue_slot_in(struct dln2_slot *slot)
{
    dln2_slot_enqueue(&dln2_response_queue, slot);
    dln2_slot_in_xfer();
}

static bool _dln2_response(struct dln2_slot *slot, size_t len, uint16_t result)
{
    LOG2("%s: len=%zu result=%u\n", __func__, len, result);

    struct dln2_response *response = dln2_slot_response(slot);
    response->hdr.size = sizeof(*response) + len;
    response->result = result;

    dln2_queue_slot_in(slot);

    return true;
}

bool dln2_response(struct dln2_slot *slot, size_t len)
{
    return _dln2_response(slot, len, 0);
}

bool dln2_response_u8(struct dln2_slot *slot, uint8_t val)
{
    memcpy(dln2_slot_response_data(slot), &val, sizeof(val));
    return _dln2_response(slot, sizeof(val), 0);
}

bool dln2_response_u16(struct dln2_slot *slot, uint16_t val)
{
    memcpy(dln2_slot_response_data(slot), &val, sizeof(val));
    return _dln2_response(slot, sizeof(val), 0);
}

bool dln2_response_u32(struct dln2_slot *slot, uint32_t val)
{
    memcpy(dln2_slot_response_data(slot), &val, sizeof(val));
    return _dln2_response(slot, sizeof(val), 0);
}

bool dln2_response_error(struct dln2_slot *slot, uint16_t result)
{
    //printf("%s: handle=%u: result=0x%x (%u)\n", __func__, dln2_slot_header(slot)->handle, result, result);
    return _dln2_response(slot, 0, result);
}

static bool dln2_handle_ctrl(struct dln2_slot *slot)
{
    struct dln2_header *hdr = dln2_slot_header(slot);
    size_t len = dln2_slot_header_data_size(slot);
    pico_unique_board_id_t board_id;
    uint64_t serial = 0;

    LOG1("DLN2_HANDLE_CTRL:\n");

    switch (hdr->id) {
    case DLN2_CMD_GET_DEVICE_VER:
        if (len)
            return dln2_response_error(slot, DLN2_RES_INVALID_COMMAND_SIZE);
        return dln2_response_u32(slot, DLN2_HW_ID);
    case DLN2_CMD_GET_DEVICE_SN:
        if (len)
            return dln2_response_error(slot, DLN2_RES_INVALID_COMMAND_SIZE);
        pico_get_unique_board_id(&board_id);
        for (uint i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++) {
            serial <<= 8;
            serial |= board_id.id[i];
        }
        return dln2_response_u32(slot, serial); // truncates
    default:
        return dln2_response_error(slot, DLN2_RES_COMMAND_NOT_SUPPORTED);
    }
}

static bool dln2_handle(struct dln2_slot *slot)
{
    struct dln2_header *hdr = dln2_slot_header(slot);

    dln2_print_slot(slot);

    switch (hdr->handle) {
    case DLN2_HANDLE_CTRL:
        return dln2_handle_ctrl(slot);
    case DLN2_HANDLE_GPIO:
        return dln2_handle_gpio(slot);
    case DLN2_HANDLE_I2C:
        return dln2_handle_i2c(slot);
    case DLN2_HANDLE_SPI:
        return dln2_handle_spi(slot);
    case DLN2_HANDLE_ADC:
        return dln2_handle_adc(slot);
    }

    return dln2_response_error(slot, DLN2_RES_INVALID_HANDLE);
}

bool dln2_xfer_out(size_t len)
{
    LOG2("%s: len=%zu\n", __func__, len);

    struct dln2_slot *slot = dln2_slot_out;
    TU_ASSERT(slot);

    dln2_slot_out = NULL;

    struct dln2_header *hdr = dln2_slot_header(slot);

    size_t slot_len = slot->len;
    slot->len += len;
    if (!slot_len) {
        if (len < sizeof(struct dln2_header)) {
            dln2_response_error(slot, DLN2_RES_INVALID_MESSAGE_SIZE);
        } else if (len < CFG_DLN2_BULK_ENPOINT_SIZE) {
            if (hdr->size != len)
                dln2_response_error(slot, DLN2_RES_INVALID_MESSAGE_SIZE);
            else
                dln2_handle(slot);
        } else if (len > CFG_DLN2_BULK_ENPOINT_SIZE) {
            dln2_response_error(slot, DLN2_RES_FAIL); // shouldn't be possible...
        } else if (hdr->size > DLN2_BUF_SIZE) {
            dln2_response_error(slot, DLN2_RES_INVALID_MESSAGE_SIZE);
        } else if (hdr->size == CFG_DLN2_BULK_ENPOINT_SIZE) {
            dln2_handle(slot);
        } else {
            bool ret = usbd_edpt_xfer(dln2_rhport, dln2_ep_out,
                                      slot->data + CFG_DLN2_BULK_ENPOINT_SIZE, hdr->size - CFG_DLN2_BULK_ENPOINT_SIZE);
            if (!ret) {
                dln2_response_error(slot, DLN2_RES_FAIL);
            } else {
                // Wait for the rest of this message
                dln2_slot_out = slot;
                return true;
            }
        }
    } else {
        if (slot->len != hdr->size)
            dln2_response_error(slot, DLN2_RES_INVALID_MESSAGE_SIZE);
        else
            dln2_handle(slot);
    }

    dln2_queue_slot_out();

    return true;
}

bool dln2_xfer_in(size_t len)
{
    LOG2("%s: len=%zu\n", __func__, len);

    struct dln2_slot *slot = dln2_slot_in;
    TU_ASSERT(slot);

    dln2_slot_in = NULL;

    //dln2_print_slot(slot);

    struct dln2_response *response = dln2_slot_response(slot);
    if (len != response->hdr.size)
        LOG1("len != response->hdr.size\n");

    dln2_put_slot(slot);

    if (!dln2_slot_out)
        dln2_queue_slot_out();

    dln2_slot_in_xfer();

    return true;
}
