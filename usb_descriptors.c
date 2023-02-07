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

#include <string.h>
#include "tusb.h"
#include "pico/unique_id.h"

enum string_index {
    RESERVED_IDX = 0,
    MANUFACTURER_IDX,
    PRODUCT_IDX,
    SERIALNUMBER_IDX,
    DLN2_INTERFACE_NAME_IDX,
    CDC_NAME_IDX,
};

enum interface_index {
    DLN_IFCE = 0,
    CDC1_CMD_IFCE,
    CDC1_DATA_IFCE,
    CDC2_CMD_IFCE,
    CDC2_DATA_IFCE,
    MAX_N_IFCE,
};

tusb_desc_device_t const device_descriptor = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB          	= 0x0110,

    .bDeviceClass    	= 0,
    .bDeviceSubClass 	= 0,
    .bDeviceProtocol 	= 0,
    .bMaxPacketSize0 	= 64,

    .idVendor        	= 0x1d50, // https://github.com/openmoko/openmoko-usb-oui#conditions
    .idProduct       	= 0x6170,
    .bcdDevice       	= 0x0011,

    .iManufacturer      = MANUFACTURER_IDX,
    .iProduct           = PRODUCT_IDX,
    .iSerialNumber 	    = SERIALNUMBER_IDX,

    .bNumConfigurations = 1,
};

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *) &device_descriptor;
}

#define DLN2_BULK_DESCRIPTOR(_addr) \
    {                                                               \
        .bLength = sizeof(tusb_desc_endpoint_t),                    \
        .bDescriptorType = TUSB_DESC_ENDPOINT,                      \
        .bEndpointAddress = _addr,                                  \
        .bmAttributes = TUSB_XFER_BULK,                             \
        .wMaxPacketSize.size = CFG_DLN2_BULK_ENPOINT_SIZE,          \
        .bInterval = 0,                                             \
    }

typedef struct TU_ATTR_PACKED {
    tusb_desc_configuration_t config;
    tusb_desc_interface_t dln_interface;
    tusb_desc_endpoint_t dln_bulk_out;
    tusb_desc_endpoint_t dln_bulk_in;
    u_int8_t cdc1_ifce_desc[TUD_CDC_DESC_LEN];
    u_int8_t cdc2_ifce_desc[TUD_CDC_DESC_LEN];
} config_descriptor_t;

static config_descriptor_t config_descriptor = {
    .config = {
        .bLength = sizeof(tusb_desc_configuration_t),
        .bDescriptorType = TUSB_DESC_CONFIGURATION,
        .wTotalLength = sizeof(config_descriptor_t),
        .bNumInterfaces = MAX_N_IFCE,
        .bConfigurationValue = 1,
        .iConfiguration = DLN2_INTERFACE_NAME_IDX,
        .bmAttributes = TU_BIT(7) | TUSB_DESC_CONFIG_ATT_SELF_POWERED,
        .bMaxPower = 100 / 2,
    },

    .dln_interface = {
        .bLength = sizeof(tusb_desc_interface_t),
        .bDescriptorType = TUSB_DESC_INTERFACE,
        .bInterfaceNumber = DLN_IFCE,
        .bAlternateSetting = 0,
        .bNumEndpoints = 2,
        .bInterfaceClass = TUSB_CLASS_VENDOR_SPECIFIC,
        .bInterfaceSubClass = 0x00,
        .bInterfaceProtocol = 0x00,
        .iInterface = 0,
    },

    .dln_bulk_out = DLN2_BULK_DESCRIPTOR(USBD_DLN_EP_OUT),
    .dln_bulk_in = DLN2_BULK_DESCRIPTOR(USBD_DLN_EP_IN),

    .cdc1_ifce_desc = {
        TUD_CDC_DESCRIPTOR(CDC1_CMD_IFCE, CDC_NAME_IDX,
            USBD_CDC_0_EP_CMD, USBD_CDC_CMD_MAX_SIZE,
            USBD_CDC_0_EP_OUT, USBD_CDC_0_EP_IN, USBD_CDC_IN_OUT_MAX_SIZE)
    },

    .cdc2_ifce_desc = {
        TUD_CDC_DESCRIPTOR(CDC2_CMD_IFCE, CDC_NAME_IDX,
            USBD_CDC_1_EP_CMD, USBD_CDC_CMD_MAX_SIZE,
            USBD_CDC_1_EP_OUT, USBD_CDC_1_EP_IN, USBD_CDC_IN_OUT_MAX_SIZE)
    },
};

uint8_t const * tud_descriptor_configuration_cb(uint8_t index)
{
    return (uint8_t const *)&config_descriptor;
}

typedef struct TU_ATTR_PACKED {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t unicode_string[31];
} dln2_desc_string_t;

static dln2_desc_string_t string_descriptor = {
    .bDescriptorType = TUSB_DESC_STRING,
};

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void) langid;

    if (index == 0) {
        string_descriptor.bLength = 4;
        string_descriptor.unicode_string[0] = 0x0409;
        return (uint16_t *)&string_descriptor;
    }

    const char *str;
    char serial[17];

    if (index == MANUFACTURER_IDX) {
        str = "Raspberry Pi";
    } else if (index == PRODUCT_IDX) {
        str = "Pico USB I/O Board";
    } else if (index == SERIALNUMBER_IDX) {
        pico_get_unique_board_id_string(serial, sizeof(serial));
        str = serial;
    } else if (index == DLN2_INTERFACE_NAME_IDX) {
        str = "DLN2";
    } else if (index == CDC_NAME_IDX) {
        str = "Pico USB CDC";
    } else {
        return NULL;
    }

    uint8_t len = strlen(str);
    if (len > sizeof(string_descriptor.unicode_string))
        len = sizeof(string_descriptor.unicode_string);

    string_descriptor.bLength = 2 + 2 * len;

    for (uint8_t i = 0; i < len; i++)
      string_descriptor.unicode_string[i] = str[i];

    return (uint16_t *)&string_descriptor;
}
