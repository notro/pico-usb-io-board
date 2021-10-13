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
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "dln2.h"

#define LOG1    printf

#define DLN2_ADC_CMD(cmd)   ((DLN2_MODULE_ADC << 8) | (cmd))

#define DLN2_ADC_GET_CHANNEL_COUNT      DLN2_ADC_CMD(0x01)
#define DLN2_ADC_ENABLE                 DLN2_ADC_CMD(0x02)
#define DLN2_ADC_DISABLE                DLN2_ADC_CMD(0x03)
#define DLN2_ADC_CHANNEL_ENABLE         DLN2_ADC_CMD(0x05)
#define DLN2_ADC_CHANNEL_DISABLE        DLN2_ADC_CMD(0x06)
#define DLN2_ADC_SET_RESOLUTION         DLN2_ADC_CMD(0x08)
#define DLN2_ADC_CHANNEL_GET_VAL        DLN2_ADC_CMD(0x0A)
#define DLN2_ADC_CHANNEL_GET_ALL_VAL    DLN2_ADC_CMD(0x0B)
#define DLN2_ADC_CHANNEL_SET_CFG        DLN2_ADC_CMD(0x0C)
#define DLN2_ADC_CONDITION_MET_EV       DLN2_ADC_CMD(0x10)

#define DLN2_ADC_EVENT_NONE         0
#define DLN2_ADC_EVENT_ALWAYS       5

#define DLN2_ADC_NUM_CHANNELS   3
#define DLN2_ADC_MAX_CHANNELS   8
//#define DLN2_ADC_DATA_BITS      10

struct dln2_adc_port_chan {
    uint8_t port;
    uint8_t chan;
} TU_ATTR_PACKED;

struct dln2_adc_get_all_vals {
    uint16_t channel_mask;
    uint16_t values[DLN2_ADC_MAX_CHANNELS];
} TU_ATTR_PACKED;

static repeating_timer_t dln2_adc_event_timer;

static uint16_t dln2_adc_read(uint input)
{
    adc_select_input(input);
    // The Linux driver has a fixed 10-bit resolution
    // It is possible to return the full value, but userspace might choke on the out of bounds value
    return adc_read() >> 2;
}

static bool dln2_adc_channel_enable(struct dln2_slot *slot, bool enable)
{
    struct dln2_adc_port_chan *port_chan = dln2_slot_header_data(slot);

    DLN2_VERIFY_COMMAND_SIZE(slot, sizeof(*port_chan));
    LOG1("%s: port=%u chan=%u\n", enable ? "DLN2_ADC_CHANNEL_ENABLE" : "DLN2_ADC_CHANNEL_DISABLE", port_chan->port, port_chan->chan);

    if (port_chan->chan >= DLN2_ADC_NUM_CHANNELS)
        dln2_response_error(slot, DLN2_RES_INVALID_CHANNEL_NUMBER);

    uint16_t pin = port_chan->chan + 26;

    if (enable) {
        int res = dln2_pin_request(pin, DLN2_MODULE_ADC);
        if (res)
            return dln2_response_error(slot, res);

        adc_gpio_init(pin);
    } else if (dln2_pin_is_requested(pin, DLN2_MODULE_ADC)) {
        int res = dln2_pin_free(pin, DLN2_MODULE_ADC);
        if (res)
            return dln2_response_error(slot, res);

        gpio_set_function(pin, GPIO_FUNC_NULL);
    }

    return dln2_response(slot, 0);
}

static bool dln2_adc_enable(struct dln2_slot *slot, bool enable)
{
    uint8_t *port = dln2_slot_header_data(slot);
    uint16_t conflict = 0;

    DLN2_VERIFY_COMMAND_SIZE(slot, sizeof(*port));
    LOG1("%s: port=%u\n", enable ? "DLN2_ADC_ENABLE" : "DLN2_ADC_DISABLE", *port);

    if (!enable) {
        cancel_repeating_timer(&dln2_adc_event_timer);
        for (uint pin = 26; pin <= 28; pin++)
            dln2_pin_free(pin, DLN2_MODULE_ADC);
    }

    put_unaligned_le16(conflict, dln2_slot_response_data(slot));
    return dln2_response(slot, sizeof(conflict));
}

static bool dln2_adc_channel_get_val(struct dln2_slot *slot)
{
    struct dln2_adc_port_chan *port_chan = dln2_slot_header_data(slot);

    DLN2_VERIFY_COMMAND_SIZE(slot, sizeof(*port_chan));
    LOG1("DLN2_ADC_CHANNEL_GET_VAL: port=%u chan=%u\n", port_chan->port, port_chan->chan);

    if (port_chan->chan >= DLN2_ADC_NUM_CHANNELS)
        dln2_response_error(slot, DLN2_RES_INVALID_CHANNEL_NUMBER);

    uint16_t value = dln2_adc_read(port_chan->chan);
    put_unaligned_le16(value, dln2_slot_response_data(slot));
    return dln2_response(slot, sizeof(value));
}

static bool dln2_adc_channel_get_all_val(struct dln2_slot *slot)
{
    uint8_t *port = dln2_slot_header_data(slot);
    uint16_t *channel_mask = dln2_slot_response_data(slot);
    uint16_t *values = channel_mask + 1;
    size_t len = sizeof(uint16_t) * (DLN2_ADC_MAX_CHANNELS + 1);

    DLN2_VERIFY_COMMAND_SIZE(slot, sizeof(*port));
    LOG1("DLN2_ADC_CHANNEL_GET_ALL_VAL: port=%u\n", *port);

    // zero the buffer to ease debugging
    memset(channel_mask, 0, len);

    // The Linux driver ignores channel_mask
    put_unaligned_le16(0x0000, channel_mask);

    // Sample time: 3x 2us ~= 6us
    for (uint i = 0; i < DLN2_ADC_NUM_CHANNELS; i++) {
        values[i] = dln2_adc_read(i);
    }

    return dln2_response(slot, len);
}

static void dln2_adc_event(void)
{
    // the Linux driver ignores these values entirely...
    struct {
        uint16_t count;
        uint8_t port;
        uint8_t chan;
        uint16_t value;
        uint8_t type;
    } TU_ATTR_PACKED *event;

    LOG1("%s:\n", __func__);

    struct dln2_slot *slot = dln2_get_slot();
    if (!slot) {
        LOG1("Run out of slots!\n");
        return;
    }

    struct dln2_header *hdr = dln2_slot_header(slot);
    hdr->size = sizeof(*hdr) + sizeof(*event);
    hdr->id = DLN2_ADC_CONDITION_MET_EV;
    hdr->echo = 0;
    hdr->handle = DLN2_HANDLE_EVENT;

    event = dln2_slot_header_data(slot);
    event->count = 0;
    event->port = 0;
    event->chan = 0;
    event->value = 0;
    event->type = 0;

    dln2_print_slot(slot);
    dln2_queue_slot_in(slot);
}

static bool dln2_adc_event_timer_callback(repeating_timer_t *rt) {
    LOG1("%s\n", __func__);
    dln2_adc_event();
    return true; // keep repeating
}

static bool dln2_adc_channel_set_cfg(struct dln2_slot *slot)
{
    struct {
        struct dln2_adc_port_chan port_chan;
        uint8_t type;
        uint16_t period;
        uint16_t low;
        uint16_t high;
    } TU_ATTR_PACKED *cfg = dln2_slot_header_data(slot);

    DLN2_VERIFY_COMMAND_SIZE(slot, sizeof(*cfg));

    LOG1("DLN2_ADC_CHANNEL_SET_CFG: port=%u chan=%u type=%u period=%ums low=%u high=%u\n",
         cfg->port_chan.port, cfg->port_chan.chan, cfg->type, cfg->period, cfg->low, cfg->high);

    if (cfg->type != DLN2_ADC_EVENT_NONE && cfg->type != DLN2_ADC_EVENT_ALWAYS) {
        LOG1("ADC event type not implemented\n");
        return dln2_response_error(slot, DLN2_RES_NOT_IMPLEMENTED);
    }

    /*
     * FIXME:
     * DLN_RES_INVALID_EVENT_PERIOD (0xAC)
     * Defining the event configuration, you specified the invalid event period.
     * It may occur, for example, if you set the zero event period for the ALWAYS event type.
     * For details, read Digital Input Events.
     */

    if (!dln2_response(slot, 0))
        return false;

    if (cfg->type == DLN2_ADC_EVENT_NONE && !cfg->period) {
        cancel_repeating_timer(&dln2_adc_event_timer);
        // send a single event
        dln2_adc_event();
    } else if (cfg->type == DLN2_ADC_EVENT_ALWAYS) {
        // negative timeout means exact delay (rather than delay between callbacks)
        if (!add_repeating_timer_us(-1000 * cfg->period, dln2_adc_event_timer_callback, NULL, &dln2_adc_event_timer)) {
            LOG1("ADC: Failed to add timer\n");
            // what must happen for this to fail?
            return false;
        }
    }

    return true;
}

bool dln2_handle_adc(struct dln2_slot *slot)
{
    struct dln2_header *hdr = dln2_slot_header(slot);

    switch (hdr->id) {
    case DLN2_ADC_GET_CHANNEL_COUNT:
        LOG1("DLN2_ADC_GET_CHANNEL_COUNT\n");
        DLN2_VERIFY_COMMAND_SIZE(slot, 1);
        // TODO: check port
        adc_init();
        return dln2_response_u8(slot, DLN2_ADC_NUM_CHANNELS);
    case DLN2_ADC_ENABLE:
        return dln2_adc_enable(slot, true);
    case DLN2_ADC_DISABLE:
        return dln2_adc_enable(slot, false);
    case DLN2_ADC_CHANNEL_ENABLE:
        return dln2_adc_channel_enable(slot, true);
    case DLN2_ADC_CHANNEL_DISABLE:
        return dln2_adc_channel_enable(slot, false);
    case DLN2_ADC_SET_RESOLUTION:
        LOG1("DLN2_ADC_SET_RESOLUTION\n");
        DLN2_VERIFY_COMMAND_SIZE(slot, 2);
        // TODO: check port and resolution
        return dln2_response(slot, 0);
    case DLN2_ADC_CHANNEL_GET_VAL:
        return dln2_adc_channel_get_val(slot);
    case DLN2_ADC_CHANNEL_GET_ALL_VAL:
        return dln2_adc_channel_get_all_val(slot);
    case DLN2_ADC_CHANNEL_SET_CFG:
        return dln2_adc_channel_set_cfg(slot);
    default:
        LOG1("ADC command not supported: 0x%04x\n", hdr->id);
        return dln2_response_error(slot, DLN2_RES_COMMAND_NOT_SUPPORTED);
    }
}
