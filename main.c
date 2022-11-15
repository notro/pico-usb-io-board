// SPDX-License-Identifier: CC0-1.0
/*
 * Written in 2021 by Noralf Trønnes <noralf@tronnes.org>
 * Extended in 2022 by Tao Jin <tao-j@outlook.com>
 *
 * To the extent possible under law, the author(s) have dedicated all copyright and related and
 * neighboring rights to this software to the public domain worldwide. This software is
 * distributed without any warranty.
 *
 * You should have received a copy of the CC0 Public Domain Dedication along with this software.
 * If not, see <http://creativecommons.org/publicdomain/zero/1.0/>.
 */

#include "bsp/board.h"
#include "tusb.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "uart.h"
#include "i2c-at24.h"

static const uint8_t eeprom10[] = "HELLO";

DEFINE_I2C_AT24C32(eeprom, 0x10, eeprom10, sizeof(eeprom10));

struct dln2_i2c_device *i2c_devices[] = {
    &eeprom.base,
    NULL,
};

int main(void)
{
    // set_sys_clock_khz(250000, false);

    uint32_t unavail_pins = (1 << 29) | /* IP Used in ADC mode (ADC3) to measure VSYS/3 */
                            (1 << 24) | /* IP VBUS sense - high if VBUS is present, else low */
                            (1 << 23) | /* OP Controls the on-board SMPS Power Save pin */
                            (1 << UART0_TX)  | 
                            (1 << UART0_RX)  | 
                            (1 << UART1_TX)  | 
                            (1 << UART1_RX);
    dln2_pin_set_available(~unavail_pins);

    dln2_gpio_init();
    dln2_i2c_set_devices(i2c_devices);

    board_init();
    tusb_init();

    multicore_launch_core1(core1_entry);

    for (int itf = 0; itf < CFG_TUD_CDC; itf++)
        init_uart_data(itf);

    if (CFG_TUSB_DEBUG)
        printf("\n\n\n\n\nPico I/O Board CFG_TUSB_DEBUG=%u\n", CFG_TUSB_DEBUG);

    while (1)
    {
        tud_task();
        dln2_gpio_task();

        for (int itf = 0; itf < NUM_UART_IFCE; itf++) {
            update_uart_cfg(itf);
            uart_write_bytes(itf);
        }
    }

    return 0;
}
