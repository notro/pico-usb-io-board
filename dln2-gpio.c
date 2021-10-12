// SPDX-License-Identifier: CC0-1.0

#include <stdio.h>
#include "pico/stdlib.h"
#include "dln2.h"

#define LOG1    printf

#define DLN2_GPIO_CMD(cmd)       DLN2_CMD(cmd, DLN2_MODULE_GPIO)

#define DLN2_GPIO_GET_PIN_COUNT         DLN2_GPIO_CMD(0x01)
#define DLN2_GPIO_SET_DEBOUNCE          DLN2_GPIO_CMD(0x04)
#define DLN2_GPIO_PIN_GET_VAL           DLN2_GPIO_CMD(0x0B)
#define DLN2_GPIO_PIN_SET_OUT_VAL       DLN2_GPIO_CMD(0x0C)
#define DLN2_GPIO_PIN_GET_OUT_VAL       DLN2_GPIO_CMD(0x0D)
#define DLN2_GPIO_CONDITION_MET_EV      DLN2_GPIO_CMD(0x0F)
#define DLN2_GPIO_PIN_ENABLE            DLN2_GPIO_CMD(0x10)
#define DLN2_GPIO_PIN_DISABLE           DLN2_GPIO_CMD(0x11)
#define DLN2_GPIO_PIN_SET_DIRECTION     DLN2_GPIO_CMD(0x13)
#define DLN2_GPIO_PIN_GET_DIRECTION     DLN2_GPIO_CMD(0x14)
#define DLN2_GPIO_PIN_SET_EVENT_CFG     DLN2_GPIO_CMD(0x1E)

#define DLN2_GPIO_EVENT_NONE            0
#define DLN2_GPIO_EVENT_CHANGE          1
#define DLN2_GPIO_EVENT_LVL_HIGH        2
#define DLN2_GPIO_EVENT_LVL_LOW         3

#define DLN2_GPIO_NUM_PINS  29

#ifdef PICO_DEFAULT_LED_PIN
  #define LED_PIN   PICO_DEFAULT_LED_PIN
#else
  #define LED_PIN   0xff    // out of bounds value that will never match
#endif

static const char *dln2_gpio_id_to_name(uint16_t id)
{
    switch (id) {
    case DLN2_GPIO_GET_PIN_COUNT:
        return "GPIO_GET_PIN_COUNT";
    case DLN2_GPIO_SET_DEBOUNCE:
        return "GPIO_SET_DEBOUNCE";
    case DLN2_GPIO_PIN_GET_VAL:
        return "GPIO_PIN_GET_VAL";
    case DLN2_GPIO_PIN_SET_OUT_VAL:
        return "GPIO_PIN_SET_OUT_VAL";
    case DLN2_GPIO_PIN_GET_OUT_VAL:
        return "GPIO_PIN_GET_OUT_VAL";
    case DLN2_GPIO_CONDITION_MET_EV:
        return "GPIO_CONDITION_MET_EV";
    case DLN2_GPIO_PIN_ENABLE:
        return "GPIO_PIN_ENABLE";
    case DLN2_GPIO_PIN_DISABLE:
        return "GPIO_PIN_DISABLE";
    case DLN2_GPIO_PIN_SET_DIRECTION:
        return "GPIO_PIN_SET_DIRECTION";
    case DLN2_GPIO_PIN_GET_DIRECTION:
        return "GPIO_PIN_GET_DIRECTION";
    case DLN2_GPIO_PIN_SET_EVENT_CFG:
        return "GPIO_PIN_SET_EVENT_CFG";
    }
    return NULL;
}

static int dln2_gpio_slot_pin_val(struct dln2_slot *slot, uint8_t *val)
{
    size_t len = dln2_slot_header_data_size(slot);
    if ((!val && len != 2) || (val && len != 3))
        return -1;

    void *data = dln2_slot_header_data(slot);
    uint16_t *pin = data;
    if (*pin > (DLN2_GPIO_NUM_PINS - 1))
        return -1;

    if (val)
        *val = *(uint8_t *)(data + 2);

    const struct dln2_header *hdr = dln2_slot_header(slot);
    const char *name = dln2_gpio_id_to_name(hdr->id);
    if (name)
        LOG1("\n%s: ", name);
    else
        LOG1("DLN_GPIO UNKNOWN 0x%02x: ", hdr->id);
    LOG1("pin=%u val=%d\n", *pin, val ? *val : -1);

    return *pin;
}

#define DLN2_GPIO_GET_PIN_VERIFY(_slot, _pin, _val)                     \
    (_pin) = dln2_gpio_slot_pin_val((_slot), (_val));                   \
    if ((_pin) < 0 || !dln2_pin_is_requested((_pin), DLN2_MODULE_GPIO))     \
        return dln2_response_error((_slot), DLN2_RES_INVALID_PIN_NUMBER)

static bool dln2_gpio_response_pin_val(struct dln2_slot *slot, uint16_t pin, uint8_t *val)
{
    uint8_t *data = dln2_slot_response_data(slot);
    put_unaligned_le16(pin, data);
    if (val)
        data[2] = *val;

    LOG1("GPIO RSP: pin=%u val=%d\n", pin, val ? *val : -1);

    return dln2_response(slot, val ? 3 : 2);
}

static bool dln2_gpio_pin_enable(struct dln2_slot *slot, bool enable)
{
    int pin = dln2_gpio_slot_pin_val(slot, NULL);
    if (pin < 0)
        return dln2_response_error(slot, DLN2_RES_INVALID_PIN_NUMBER);

    LOG1("%s: pin=%u enable=%u\n", __func__, pin, enable);
    if (enable) {
        int res = dln2_pin_request(pin, DLN2_MODULE_GPIO);
        if (res)
            return dln2_response_error(slot, res);

        enum gpio_function fn = gpio_get_function(pin);
        LOG1("    gpio_get_function=%u\n", fn);

        if (pin != LED_PIN) {
            gpio_set_function(pin, GPIO_FUNC_SIO);
            gpio_set_dir(pin, GPIO_IN);
            // DLN-2 default:
            gpio_pull_up(pin);
        }
    } else {
        int res = dln2_pin_free(pin, DLN2_MODULE_GPIO);
        if (res)
            return dln2_response_error(slot, res);
        if (pin != LED_PIN)
            gpio_set_function(pin, GPIO_FUNC_NULL);
    }
    return dln2_response(slot, 0);
}

static bool dln2_gpio_pin_set_event_cfg(struct dln2_slot *slot)
{
    struct {
        uint16_t pin;
        uint8_t type;
        uint16_t period;
    } TU_ATTR_PACKED *cmd = dln2_slot_header_data(slot);

    DLN2_VERIFY_COMMAND_SIZE(slot, sizeof(*cmd));

    LOG1("\nDLN2_GPIO_PIN_SET_EVENT_CFG: pin=%u type=%u period=%u\n", cmd->pin, cmd->type, cmd->period);

    if (!dln2_pin_is_requested(cmd->pin, DLN2_MODULE_GPIO))
        return dln2_response_error(slot, DLN2_RES_INVALID_PIN_NUMBER);

    if (cmd->pin == LED_PIN)
        return dln2_response_error(slot, DLN2_RES_INVALID_VALUE);

    switch (cmd->type) {
    case DLN2_GPIO_EVENT_NONE:
        gpio_set_irq_enabled(cmd->pin, GPIO_IRQ_LEVEL_LOW | GPIO_IRQ_LEVEL_HIGH | GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, false);
        break;
    case DLN2_GPIO_EVENT_CHANGE:
        gpio_set_irq_enabled(cmd->pin, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true);
        break;
    case DLN2_GPIO_EVENT_LVL_HIGH:
        gpio_set_irq_enabled(cmd->pin, GPIO_IRQ_LEVEL_HIGH, true);
        break;
    case DLN2_GPIO_EVENT_LVL_LOW:
        gpio_set_irq_enabled(cmd->pin, GPIO_IRQ_LEVEL_LOW, true);
        break;
    default:
        return dln2_response_error(slot, DLN2_RES_INVALID_EVENT_TYPE);
    }

    return dln2_response(slot, 0);
}

bool dln2_handle_gpio(struct dln2_slot *slot)
{
    struct dln2_header *hdr = dln2_slot_header(slot);
    uint8_t val;
    int pin;

    switch (hdr->id) {
    case DLN2_GPIO_GET_PIN_COUNT:
        LOG1("DLN2_GPIO_GET_PIN_COUNT\n");
        if (dln2_slot_header_data_size(slot))
            return dln2_response_error(slot, DLN2_RES_INVALID_COMMAND_SIZE);
        return dln2_response_u16(slot, DLN2_GPIO_NUM_PINS);
    case DLN2_GPIO_SET_DEBOUNCE:
/*
 * Linux:
 * @PIN_CONFIG_INPUT_DEBOUNCE: this will configure the pin to debounce mode,
 *  which means it will wait for signals to settle when reading inputs. The
 *  argument gives the debounce time in usecs. Setting the
 *  argument to zero turns debouncing off.
 */
        LOG1("DLN2_GPIO_SET_DEBOUNCE\n");
        return dln2_response_error(slot, DLN2_RES_COMMAND_NOT_SUPPORTED);
    case DLN2_GPIO_PIN_GET_VAL:
        DLN2_GPIO_GET_PIN_VERIFY(slot, pin, NULL);
        val = gpio_get(pin);
        return dln2_gpio_response_pin_val(slot, pin, &val);
    case DLN2_GPIO_PIN_SET_OUT_VAL:
        DLN2_GPIO_GET_PIN_VERIFY(slot, pin, &val);
        gpio_put(pin, val);
        return dln2_gpio_response_pin_val(slot, pin, NULL);
    case DLN2_GPIO_PIN_GET_OUT_VAL:
        DLN2_GPIO_GET_PIN_VERIFY(slot, pin, NULL);
        val = gpio_get_out_level(pin);
        return dln2_gpio_response_pin_val(slot, pin, &val);
    case DLN2_GPIO_PIN_ENABLE:
        return dln2_gpio_pin_enable(slot, true);
    case DLN2_GPIO_PIN_DISABLE:
        return dln2_gpio_pin_enable(slot, false);
    case DLN2_GPIO_PIN_SET_DIRECTION:
        DLN2_GPIO_GET_PIN_VERIFY(slot, pin, &val);
        if (pin == LED_PIN && !val)
            return dln2_response_error(slot, DLN2_RES_INVALID_VALUE);
        gpio_set_dir(pin, val);
        return dln2_gpio_response_pin_val(slot, pin, NULL);
    case DLN2_GPIO_PIN_GET_DIRECTION:
        DLN2_GPIO_GET_PIN_VERIFY(slot, pin, NULL);
        val = gpio_get_dir(pin);
        return dln2_gpio_response_pin_val(slot, pin, &val);
    case DLN2_GPIO_PIN_SET_EVENT_CFG:
        return dln2_gpio_pin_set_event_cfg(slot);
    default:
        LOG1("GPIO command not supported: 0x%04x\n", hdr->id);
        return dln2_response_error(slot, DLN2_RES_COMMAND_NOT_SUPPORTED);
    }
}


/*
#define DLN2_GPIO_EVENT_NONE            0
#define DLN2_GPIO_EVENT_CHANGE          1
#define DLN2_GPIO_EVENT_LVL_HIGH        2
#define DLN2_GPIO_EVENT_LVL_LOW         3

    GPIO_IRQ_LEVEL_LOW = 0x1u,
    GPIO_IRQ_LEVEL_HIGH = 0x2u,
    GPIO_IRQ_EDGE_FALL = 0x4u,
    GPIO_IRQ_EDGE_RISE = 0x8u,

dln2_gpio_event: gpio=12 events=0x4
dln2_gpio_event: gpio=12 events=0xc
dln2_gpio_event: gpio=12 events=0x8


dln2_gpio_event: gpio=12 events=0x4
dln2_gpio_event: gpio=12 events=0x8

*/

// TODO: Handle rise and fall set in the same interrupt: events=0xc

void dln2_gpio_event(uint gpio, uint32_t events) {
    // Couldn't find this in the docs, the Linux driver ignores count and type, what are they for?
    struct {
        uint16_t count;
        uint8_t type;
        uint16_t pin;
        uint8_t value;
    } TU_ATTR_PACKED *event;

    LOG1("%s: gpio=%u events=0x%x\n", __func__, gpio, events);

    struct dln2_slot *slot = dln2_get_slot();
    if (!slot) {
        LOG1("Run out of slots!\n");
        return;
    }

    struct dln2_header *hdr = dln2_slot_header(slot);
    hdr->size = sizeof(*hdr) + sizeof(*event);
    hdr->id = DLN2_GPIO_CONDITION_MET_EV;
    hdr->echo = 0;
    hdr->handle = DLN2_HANDLE_EVENT;

    event = dln2_slot_header_data(slot);
    event->count = 0;
    event->type = 0;
    event->pin = gpio;
    event->value = gpio_get(gpio);

    dln2_print_slot(slot);
    dln2_queue_slot_in(slot);
}

void dln2_gpio_init(void)
{
    // It's not possible to just set the callback, so use the board led as a dummy gpio
    gpio_set_irq_enabled_with_callback(25, 0, false, &dln2_gpio_event);
}
