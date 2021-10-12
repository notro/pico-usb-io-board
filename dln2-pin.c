// SPDX-License-Identifier: CC0-1.0

#include <stdio.h>
#include "dln2.h"

#define LOG1    printf

#define DLN2_PIN_MAX    32
#define DLN2_PIN_NOT_AVAILABLE  0xff

struct dln2_pin_state {
    uint8_t module;
};

static struct dln2_pin_state dln2_pin_states[DLN2_PIN_MAX];

bool dln2_pin_is_requested(uint16_t pin, uint8_t module)
{
    if (pin >= DLN2_PIN_MAX)
        return false;

    struct dln2_pin_state *state = &dln2_pin_states[pin];
    return state->module == module;
}

uint16_t dln2_pin_request(uint16_t pin, uint8_t module)
{
    if (pin >= DLN2_PIN_MAX)
        return DLN2_RES_INVALID_PIN_NUMBER;

    struct dln2_pin_state *state = &dln2_pin_states[pin];
    if (state->module && state->module != module)
        return DLN2_RES_PIN_IN_USE;

    state->module = module;
    return 0;
}

uint16_t dln2_pin_free(uint16_t pin, uint8_t module)
{
    if (pin >= DLN2_PIN_MAX)
        return DLN2_RES_INVALID_PIN_NUMBER;

    struct dln2_pin_state *state = &dln2_pin_states[pin];
    if (state->module && state->module != module)
        return DLN2_RES_PIN_NOT_CONNECTED_TO_MODULE;

    state->module = 0;
    return 0;
}

void dln2_pin_set_available(uint32_t mask)
{
    for (uint i = 0; i < DLN2_PIN_MAX; i++) {
        if (!(mask & 1))
            dln2_pin_states[i].module = DLN2_PIN_NOT_AVAILABLE;
        mask >>= 1;
    }
    dln2_pin_states[30].module = DLN2_PIN_NOT_AVAILABLE;
    dln2_pin_states[31].module = DLN2_PIN_NOT_AVAILABLE;
}
