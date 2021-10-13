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

#include "tusb_option.h"
#include "device/usbd_pvt.h"
#include "dln2.h"

#define LOG1    //printf
#define LOG2    //printf

static uint8_t _bulk_in;
static uint8_t _bulk_out;

static void driver_init(void)
{
}

static void driver_reset(uint8_t rhport)
{
    (void) rhport;
}

static void driver_disable_endpoint(uint8_t rhport, uint8_t *ep_addr)
{
    if (*ep_addr) {
        usbd_edpt_close(rhport, *ep_addr);
        *ep_addr = 0;
    }
}

static uint16_t driver_open(uint8_t rhport, tusb_desc_interface_t const * itf_desc, uint16_t max_len)
{
    LOG1("%s: bInterfaceNumber=%u max_len=%u\n", __func__, itf_desc->bInterfaceNumber, max_len);

    TU_VERIFY(TUSB_CLASS_VENDOR_SPECIFIC == itf_desc->bInterfaceClass, 0);

    uint16_t const len = sizeof(tusb_desc_interface_t) + itf_desc->bNumEndpoints * sizeof(tusb_desc_endpoint_t);
    TU_VERIFY(max_len >= len, 0);

    uint8_t const * p_desc = tu_desc_next(itf_desc);
    TU_ASSERT( usbd_open_edpt_pair(rhport, p_desc, 2, TUSB_XFER_BULK, &_bulk_out, &_bulk_in) );

    TU_ASSERT ( dln2_init(rhport, _bulk_out, _bulk_in) );

    LOG2("\n\n\n\n");

    return len;
}

static bool driver_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const * req)
{
    return false;
}

static bool driver_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes)
{
    LOG1("\n%s: ep_addr=0x%02x result=%u xferred_bytes=%u\n", __func__, ep_addr, result, xferred_bytes);

    TU_VERIFY(result == XFER_RESULT_SUCCESS);

    if (!xferred_bytes)
        LOG2("                 ZLP\n");

    if (ep_addr == _bulk_out)
        return dln2_xfer_out(xferred_bytes);
    else if (ep_addr == _bulk_in)
        return dln2_xfer_in(xferred_bytes);

    return true;
}

static usbd_class_driver_t const _driver_driver[] =
{
    {
  #if CFG_TUSB_DEBUG >= 2
        .name             = "io-board",
  #endif
        .init             = driver_init,
        .reset            = driver_reset,
        .open             = driver_open,
        .control_xfer_cb  = driver_control_xfer_cb,
        .xfer_cb          = driver_xfer_cb,
        .sof              = NULL
    },
};

usbd_class_driver_t const* usbd_app_driver_get_cb(uint8_t* driver_count)
{
	*driver_count += TU_ARRAY_SIZE(_driver_driver);

	return _driver_driver;
}
