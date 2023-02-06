// SPDX-License-Identifier: CC0-1.0
/*
 * Written in 2023 by Noralf Trønnes <noralf@tronnes.org>
 *
 * To the extent possible under law, the author(s) have dedicated all copyright and related and
 * neighboring rights to this software to the public domain worldwide. This software is
 * distributed without any warranty.
 *
 * You should have received a copy of the CC0 Public Domain Dedication along with this software.
 * If not, see <http://creativecommons.org/publicdomain/zero/1.0/>.
 */

#ifndef _CDC_UART_H_
#define _CDC_UART_H_

#ifndef UART0_TX_PIN
#define UART0_TX_PIN 0
#endif
#ifndef UART0_RX_PIN
#define UART0_RX_PIN 1
#endif
#ifndef UART1_TX_PIN
#define UART1_TX_PIN 8
#endif
#ifndef UART1_RX_PIN
#define UART1_RX_PIN 9
#endif

void cdc_uart_task(void);

#endif
