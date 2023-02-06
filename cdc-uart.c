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

#include "bsp/board.h"
#include <hardware/uart.h>
#include <pico/stdlib.h>
#include <tusb.h>

#include "dln2.h"
#include "cdc-uart.h"

void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const* p_line_coding)
{
    uart_inst_t *uart = uart_get_instance(itf);
    uint8_t data_bits = p_line_coding->data_bits;
    uint8_t stop_bits = p_line_coding->stop_bits;
    uint8_t parity;

    // tinyusb: can be 5, 6, 7, 8 or 16
    // RP2040:  Number of bits of data. 5..8
    if (data_bits < 5 || data_bits > 8)
        data_bits = 8;

    // tinyusb: 0: 1 stop bit - 1: 1.5 stop bits - 2: 2 stop bits
    // RP2040:  Number of stop bits 1..2
    if (stop_bits != 2)
        stop_bits = 1;

    // tinyusb: 0: None - 1: Odd - 2: Even - 3: Mark - 4: Space
    // RP2040:  UART_PARITY_NONE=0, UART_PARITY_EVEN=1, UART_PARITY_ODD=2
    switch (p_line_coding->parity) {
        case 1:
            parity = UART_PARITY_ODD;
            break;
        case 2:
            parity = UART_PARITY_EVEN;
            break;
        default:
            parity = UART_PARITY_NONE;
            break;
    }

    // p_line_coding is const so looks like it's not expected to set the actual baud rate.
    // The Linux driver does not support USB_CDC_REQ_GET_LINE_CODING so it doesn't matter.
    uint __unused actual = uart_set_baudrate(uart, p_line_coding->bit_rate);
    uart_set_format(uart, data_bits, stop_bits, parity);
}

// When a port is opened using pyserial this function is called with dtr=true and rts=true.
// It is called before tud_cdc_line_coding_cb()
// When the port is closed it is called with dtr=false and rts=false.
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
    const uint tx_pin = !itf ? UART0_TX_PIN : UART1_TX_PIN;
    const uint rx_pin = !itf ? UART0_RX_PIN : UART1_RX_PIN;
    uart_inst_t *uart = uart_get_instance(itf);
    cdc_line_coding_t line_coding;

    if (!dtr || dln2_pin_is_requested(tx_pin, DLN2_MODULE_UART))
        return;

    // There's no way to tell the host that the pins are in use
    if (dln2_pin_request(tx_pin, DLN2_MODULE_UART))
        return;

    if (dln2_pin_request(rx_pin, DLN2_MODULE_UART)) {
        dln2_pin_free(tx_pin, DLN2_MODULE_UART);
        return;
    }

    gpio_set_function(tx_pin, GPIO_FUNC_UART);
    gpio_set_function(rx_pin, GPIO_FUNC_UART);

    tud_cdc_n_get_line_coding(itf, &line_coding);
    uart_init(uart, line_coding.bit_rate);
    uart_set_hw_flow(uart, false, false);
    // TODO: This might not be necessary, maybe the host driver always sets the line coding when
    //       opening the port.
    tud_cdc_line_coding_cb(itf, &line_coding);
}

void tud_cdc_send_break_cb(uint8_t itf, uint16_t duration_ms)
{
    uart_inst_t *uart = uart_get_instance(itf);

    // Linux handles the duration by first sending 0xffff, wait and then sending 0.
    // drivers/tty/tty_io.c:send_break()
    // drivers/usb/class/cdc-acm.c:acm_tty_break_ctl()
    if (duration_ms == 0xffff)
        uart_set_break(uart, true);
    else if (duration_ms == 0)
        uart_set_break(uart, false);
}

static void uart_write_bytes(uint8_t itf)
{
    uart_inst_t *uart = uart_get_instance(itf);
    uint8_t chr;

    while (uart_is_writable(uart) && tud_cdc_n_read(itf, &chr, 1))
        uart_putc_raw(uart, chr);
}

static void cdc_write_bytes(uint8_t itf)
{
    uart_inst_t *uart = uart_get_instance(itf);
    unsigned int uart_count = 0, cdc_count;
    uint8_t buf[32];

    while (uart_is_readable(uart) && uart_count < sizeof(buf))
        buf[uart_count++] = uart_getc(uart);

    if (!uart_count)
        return;

    // Light up the onboard LED as an RX FIFO overflow warning
    if (uart_count == 32)
        gpio_put(PICO_DEFAULT_LED_PIN, 1);

    cdc_count = tud_cdc_n_write(itf, buf, uart_count);
    if (cdc_count)
        tud_cdc_n_write_flush(itf);
}

void cdc_uart_task(void)
{
    for (int itf = 0; itf < CFG_TUD_CDC; itf++) {
        if (!tud_cdc_n_connected(itf))
            continue;
        cdc_write_bytes(itf);
        uart_write_bytes(itf);
    }
}
