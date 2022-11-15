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

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#define CFG_TUSB_RHPORT0_MODE       (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

#define CFG_DLN2_BULK_ENPOINT_SIZE  64

//#undef CFG_TUSB_DEBUG
//#define CFG_TUSB_DEBUG              2

#define CFG_TUD_CDC 2

#define CFG_TUD_CDC_RX_BUFSIZE 1024
#define CFG_TUD_CDC_TX_BUFSIZE 1024
#define USBD_CDC_CMD_MAX_SIZE 8
#define USBD_CDC_IN_OUT_MAX_SIZE 64

#define USBD_CDC_0_EP_CMD 0x81
#define USBD_CDC_1_EP_CMD 0x83

#define USBD_CDC_0_EP_OUT 0x01
#define USBD_CDC_1_EP_OUT 0x03

#define USBD_CDC_0_EP_IN 0x82
#define USBD_CDC_1_EP_IN 0x84

#define USBD_DLN_EP_IN 0x89
#define USBD_DLN_EP_OUT 0x09

#endif /* _TUSB_CONFIG_H_ */
