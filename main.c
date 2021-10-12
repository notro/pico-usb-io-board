// SPDX-License-Identifier: CC0-1.0

#include "bsp/board.h"
#include "tusb.h"
#include "pico/stdlib.h"

#include "i2c-at24.h"

static const uint8_t eeprom10[] = "HELLO";

DEFINE_I2C_AT24C32(eeprom, 0x10, eeprom10, sizeof(eeprom10));

struct dln2_i2c_device *i2c_devices[] = {
    &eeprom.base,
    NULL,
};

int main(void)
{
    uint32_t unavail_pins = (1 << 29) | /* IP Used in ADC mode (ADC3) to measure VSYS/3 */
                            (1 << 24) | /* IP VBUS sense - high if VBUS is present, else low */
                            (1 << 23) | /* OP Controls the on-board SMPS Power Save pin */
                            (1 << 1)  | /* Debug UART */
                            (1 << 0);   /* Debug UART */
    dln2_pin_set_available(~unavail_pins);

    dln2_gpio_init();
    dln2_i2c_set_devices(i2c_devices);

    board_init();
    tusb_init();

    printf("\n\n\n\n\nPico I/O Board CFG_TUSB_DEBUG=%u\n", CFG_TUSB_DEBUG);

    while (1)
    {
        tud_task();
    }

    return 0;
}
