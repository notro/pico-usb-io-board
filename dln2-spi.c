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
#include "dln2.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"

#define LOG1    //printf

#define DLN2_SPI_DEFAULT_FREQUENCY  (1 * 1000 * 1000) // 1MHz

#define DLN2_SPI_CMD(cmd)       DLN2_CMD(cmd, DLN2_MODULE_SPI)

/* SPI commands used by the Linux driver */
#define DLN2_SPI_ENABLE                         DLN2_SPI_CMD(0x11)
#define DLN2_SPI_DISABLE                        DLN2_SPI_CMD(0x12)
#define DLN2_SPI_SET_MODE                       DLN2_SPI_CMD(0x14)
#define DLN2_SPI_SET_FRAME_SIZE                 DLN2_SPI_CMD(0x16)
#define DLN2_SPI_SET_FREQUENCY                  DLN2_SPI_CMD(0x18)
#define DLN2_SPI_READ_WRITE                     DLN2_SPI_CMD(0x1A)
#define DLN2_SPI_READ                           DLN2_SPI_CMD(0x1B)
#define DLN2_SPI_WRITE                          DLN2_SPI_CMD(0x1C)
#define DLN2_SPI_SET_SS                         DLN2_SPI_CMD(0x26)
#define DLN2_SPI_SS_MULTI_ENABLE                DLN2_SPI_CMD(0x38)
#define DLN2_SPI_SS_MULTI_DISABLE               DLN2_SPI_CMD(0x39)
#define DLN2_SPI_GET_SUPPORTED_FRAME_SIZES      DLN2_SPI_CMD(0x43)
#define DLN2_SPI_GET_SS_COUNT                   DLN2_SPI_CMD(0x44)
#define DLN2_SPI_GET_MIN_FREQUENCY              DLN2_SPI_CMD(0x45)
#define DLN2_SPI_GET_MAX_FREQUENCY              DLN2_SPI_CMD(0x46)

#define DLN2_SPI_CPHA                           (1 << 0)
#define DLN2_SPI_CPOL                           (1 << 1)

#define DLN2_SPI_MAX_XFER_SIZE          256
#define DLN2_SPI_ATTR_LEAVE_SS_LOW      (1 << 0)

#define div_round_up(n,d)   (((n) + (d) - 1) / (d))

struct {
    uint32_t freq;
    uint8_t mode;
    uint8_t bpw;
} dln2_spi_config;

static uint8_t dln2_spi_tmp_buf[DLN2_SPI_MAX_XFER_SIZE];

static bool dln2_spi_enable(struct dln2_slot *slot, bool enable)
{
    uint8_t *port = dln2_slot_header_data(slot);
    // wait_for_completion is always DLN2_TRANSFERS_WAIT_COMPLETE in the Linux driver
    //uint8_t *wait_for_completion = dln2_slot_header_data(slot) + 1;
    uint sck = PICO_DEFAULT_SPI_SCK_PIN;
    uint mosi = PICO_DEFAULT_SPI_TX_PIN;
    uint miso = PICO_DEFAULT_SPI_RX_PIN;
    int res;

    LOG1("%s: port=%u\n", enable ? "DLN2_SPI_ENABLE" : "DLN2_SPI_DISABLE", *port);

    if (enable)
        DLN2_VERIFY_COMMAND_SIZE(slot, 1);
    else
        DLN2_VERIFY_COMMAND_SIZE(slot, 2);

    if (*port)
        return dln2_response_error(slot, DLN2_RES_INVALID_PORT_NUMBER);

    if (enable) {
        res = dln2_pin_request(sck, DLN2_MODULE_SPI);
        if (res)
            return dln2_response_error(slot, res);

        res = dln2_pin_request(mosi, DLN2_MODULE_SPI);
        if (res) {
            dln2_pin_free(sck, DLN2_MODULE_SPI);
            return dln2_response_error(slot, res);
        }

        res = dln2_pin_request(miso, DLN2_MODULE_SPI);
        if (res) {
            dln2_pin_free(sck, DLN2_MODULE_SPI);
            dln2_pin_free(mosi, DLN2_MODULE_SPI);
            return dln2_response_error(slot, res);
        }

        uint freq = spi_init(spi_default, dln2_spi_config.freq);
        LOG1("SPI: actual frequency: %uHz\n", freq);

        spi_set_format(spi_default, dln2_spi_config.bpw, dln2_spi_config.mode & DLN2_SPI_CPOL,
                       dln2_spi_config.mode & DLN2_SPI_CPHA, SPI_MSB_FIRST);

        gpio_set_function(sck, GPIO_FUNC_SPI);
        gpio_set_function(mosi, GPIO_FUNC_SPI);
        gpio_set_function(miso, GPIO_FUNC_SPI);
    } else {
        res = dln2_pin_free(sck, DLN2_MODULE_SPI);
        if (res)
            return dln2_response_error(slot, res);
        gpio_set_function(sck, GPIO_FUNC_NULL);

        res = dln2_pin_free(mosi, DLN2_MODULE_SPI);
        if (res)
            return dln2_response_error(slot, res);
        gpio_set_function(mosi, GPIO_FUNC_NULL);

        res = dln2_pin_free(miso, DLN2_MODULE_SPI);
        if (res)
            return dln2_response_error(slot, res);
        gpio_set_function(miso, GPIO_FUNC_NULL);
    }

    return dln2_response(slot, 0);
}

static bool dln2_spi_set_mode(struct dln2_slot *slot)
{
    struct {
        uint8_t port;
        uint8_t mode;
    } *cmd = dln2_slot_header_data(slot);
    uint8_t mask = DLN2_SPI_CPOL | DLN2_SPI_CPHA;

    DLN2_VERIFY_COMMAND_SIZE(slot, sizeof(*cmd));

    LOG1("DLN2_SPI_SET_MODE: port=%u mode=0x%02x\n", cmd->port, cmd->mode);

    if (cmd->port)
        return dln2_response_error(slot, DLN2_RES_INVALID_PORT_NUMBER);

    if (cmd->mode & ~mask)
        return dln2_response_error(slot, DLN2_RES_INVALID_MODE);

    dln2_spi_config.mode = cmd->mode;

    return dln2_response(slot, 0);
}

static bool dln2_spi_set_bpw(struct dln2_slot *slot)
{
    struct {
        uint8_t port;
        uint8_t bpw;
    } *cmd = dln2_slot_header_data(slot);

    DLN2_VERIFY_COMMAND_SIZE(slot, sizeof(*cmd));

    LOG1("DLN2_SPI_SET_BPW: port=%u bpw=%u\n", cmd->port, cmd->bpw);

    if (cmd->port)
        return dln2_response_error(slot, DLN2_RES_INVALID_PORT_NUMBER);

    // TODO: verify
    // DLN2_RES_SPI_INVALID_FRAME_SIZE
    dln2_spi_config.bpw = cmd->bpw;

    return dln2_response(slot, 0);
}

static uint dln2_spi_min_frequency(void)
{
    uint freq_in = clock_get_hz(clk_peri);
    uint prescale = 254, postdiv = 256;

    return freq_in / (prescale * postdiv);
}

static uint dln2_spi_max_frequency(void)
{
    uint freq_in = clock_get_hz(clk_peri);
    uint prescale = 2, postdiv = 1;

    return freq_in / (prescale * postdiv);
}

static bool dln2_spi_set_frequency(struct dln2_slot *slot)
{
    struct {
        uint8_t port;
        uint32_t speed;
    } TU_ATTR_PACKED *cmd = dln2_slot_header_data(slot);
    uint32_t speed;

    DLN2_VERIFY_COMMAND_SIZE(slot, sizeof(*cmd));

    LOG1("DLN2_SPI_SET_FREQUENCY: port=%u speed=%u\n", cmd->port, cmd->speed);

    if (cmd->port)
        return dln2_response_error(slot, DLN2_RES_INVALID_PORT_NUMBER);

    if (cmd->speed < dln2_spi_min_frequency())
        cmd->speed = dln2_spi_min_frequency();
    else if (cmd->speed > dln2_spi_max_frequency())
        cmd->speed = dln2_spi_max_frequency();

    speed = spi_set_baudrate(spi_default, cmd->speed);
    LOG1("SPI: actual frequency: %uHz\n", speed);
    dln2_spi_config.freq = speed;

    // The Linux driver ignores the returned value
    return dln2_response_u32(slot, speed);
}

static void dln2_spi_cs_active(bool active)
{
    // http://dlnware.com/dll/DlnSpiMasterSetDelayAfterSS
    // With a 0ns delay time, the actual delay will be equal to 1/2 of the SPI clock frequency
    uint64_t us = div_round_up(dln2_spi_config.freq, 2 * 1000000);
    LOG1("    CS=%s wait=%llu\n", active ? "activate" : "deactivate", us);

    if (!active)
        sleep_us(us);

    gpio_put(PICO_DEFAULT_SPI_CSN_PIN, !active);

    if (active)
        sleep_us(us);
}

static bool dln2_spi_read_write(struct dln2_slot *slot)
{
    struct {
        uint8_t port;
        uint16_t size;
        uint8_t attr;
        uint8_t buf[DLN2_SPI_MAX_XFER_SIZE];
    } TU_ATTR_PACKED *cmd = dln2_slot_header_data(slot);
    uint8_t attr = cmd->attr;
    uint16_t *size = dln2_slot_response_data(slot);
    uint8_t *buf = dln2_slot_response_data(slot) + sizeof(*size);

    size_t len = dln2_slot_header_data_size(slot);
    if (len < 4)
        return dln2_response_error(slot, DLN2_RES_INVALID_COMMAND_SIZE);

    LOG1("DLN2_SPI_READ_WRITE: port=%u size=%u attr=0x%02x\n", cmd->port, cmd->size, cmd->attr);

    if (cmd->port)
        return dln2_response_error(slot, DLN2_RES_INVALID_PORT_NUMBER);
    if (cmd->size > DLN2_SPI_MAX_XFER_SIZE)
        return dln2_response_error(slot, DLN2_RES_BAD_PARAMETER);
    if (cmd->size != (len - 4))
        return dln2_response_error(slot, DLN2_RES_INVALID_BUFFER_SIZE);

    dln2_spi_cs_active(true);

    // The buffer addresses are 32-bit aligned and can be used directly.
    // It looks like txbuf can move ahead 8 bytes on rxbuf so we can't use buf directly
    spi_write_read_blocking(spi_default, cmd->buf, dln2_spi_tmp_buf, cmd->size);

    if (!(attr & DLN2_SPI_ATTR_LEAVE_SS_LOW))
        dln2_spi_cs_active(false);

    memcpy(buf, dln2_spi_tmp_buf, cmd->size);

    return dln2_response(slot, sizeof(uint16_t) + cmd->size);
}

static bool dln2_spi_read(struct dln2_slot *slot)
{
    struct {
        uint8_t port;
        uint16_t size;
        uint8_t attr;
    } TU_ATTR_PACKED *cmd = dln2_slot_header_data(slot);
    size_t len = cmd->size;
    uint8_t attr = cmd->attr;
    uint16_t *size = dln2_slot_response_data(slot);
    uint8_t *buf = dln2_slot_response_data(slot) + sizeof(*size);

    DLN2_VERIFY_COMMAND_SIZE(slot, sizeof(*cmd));
    LOG1("DLN2_SPI_READ: port=%u size=%zu attr=0x%02x\n", cmd->port, len, cmd->attr);

    if (cmd->port)
        return dln2_response_error(slot, DLN2_RES_INVALID_PORT_NUMBER);
    if (len > DLN2_SPI_MAX_XFER_SIZE)
        return dln2_response_error(slot, DLN2_RES_BAD_PARAMETER);

    put_unaligned_le16(len, size);

    dln2_spi_cs_active(true);

    // The buffer address is 32-bit aligned and can be used directly
    spi_read_blocking(spi_default, 0, buf, len);

    if (!(attr & DLN2_SPI_ATTR_LEAVE_SS_LOW))
        dln2_spi_cs_active(false);

    return dln2_response(slot, sizeof(uint16_t) + len);
}

static bool dln2_spi_write(struct dln2_slot *slot)
{
    struct {
        uint8_t port;
        uint16_t size;
        uint8_t attr;
        uint8_t buf[DLN2_SPI_MAX_XFER_SIZE];
    } TU_ATTR_PACKED *cmd = dln2_slot_header_data(slot);

    size_t len = dln2_slot_header_data_size(slot);
    if (len < 4)
        return dln2_response_error(slot, DLN2_RES_INVALID_COMMAND_SIZE);

    LOG1("DLN2_SPI_WRITE: port=%u size=%u attr=0x%02x\n", cmd->port, cmd->size, cmd->attr);

    if (cmd->port)
        return dln2_response_error(slot, DLN2_RES_INVALID_PORT_NUMBER);
    if (cmd->size > DLN2_SPI_MAX_XFER_SIZE)
        return dln2_response_error(slot, DLN2_RES_BAD_PARAMETER);
    if (cmd->size != (len - 4))
        return dln2_response_error(slot, DLN2_RES_INVALID_BUFFER_SIZE);

    dln2_spi_cs_active(true);

    // The buffer address is 32-bit aligned and can be used directly
    spi_write_blocking(spi_default, cmd->buf, cmd->size);

    if (!(cmd->attr & DLN2_SPI_ATTR_LEAVE_SS_LOW))
        dln2_spi_cs_active(false);

    return dln2_response(slot, 0);
}

static bool dln2_spi_set_ss(struct dln2_slot *slot)
{
    struct {
        uint8_t port;
        uint8_t cs_mask;
    } *cmd = dln2_slot_header_data(slot);

    DLN2_VERIFY_COMMAND_SIZE(slot, sizeof(*cmd));

    LOG1("DLN2_SPI_SET_SS: port=%u cs_mask=0x%02x\n", cmd->port, cmd->cs_mask);

    if (cmd->port)
        return dln2_response_error(slot, DLN2_RES_INVALID_PORT_NUMBER);

    if (cmd->cs_mask & 0xFE != 0xFE)
        return dln2_response_error(slot, DLN2_RES_SPI_MASTER_INVALID_SS_VALUE);

    // Nothing to do since there's only one chip select

    return dln2_response(slot, 0);
}

static bool dln2_spi_ss_multi_enable(struct dln2_slot *slot, bool enable)
{
    struct {
        uint8_t port;
        uint8_t cs_mask;
    } *cmd = dln2_slot_header_data(slot);
    uint cs = PICO_DEFAULT_SPI_CSN_PIN;

    DLN2_VERIFY_COMMAND_SIZE(slot, sizeof(*cmd));

    LOG1("%s: port=%u cs_mask=0x%02x\n", enable ? "DLN2_SPI_SS_MULTI_ENABLE" : "DLN2_SPI_SS_MULTI_DISABLE", cmd->port, cmd->cs_mask);

    if (cmd->port)
        return dln2_response_error(slot, DLN2_RES_INVALID_PORT_NUMBER);

    if (cmd->cs_mask != 0x01)
        return dln2_response_error(slot, DLN2_RES_SPI_MASTER_INVALID_SS_VALUE);

    if (enable) {
        int res = dln2_pin_request(cs, DLN2_MODULE_SPI);
        if (res)
            return dln2_response_error(slot, res);

        gpio_init(cs);
        gpio_set_dir(cs, GPIO_OUT);
        gpio_put(cs, 1);
    } else {
        int res = dln2_pin_free(cs, DLN2_MODULE_SPI);
        if (res)
            return dln2_response_error(slot, res);

        gpio_set_function(cs, GPIO_FUNC_NULL);
    }

    return dln2_response(slot, 0);
}

static bool dln2_spi_get_supported_frame_sizes(struct dln2_slot *slot)
{
    uint8_t *port = dln2_slot_header_data(slot);
    uint8_t *data = dln2_slot_response_data(slot);
    int i, j;

    LOG1("DLN2_SPI_GET_SUPPORTED_FRAME_SIZES: port=%u\n", *port);
    DLN2_VERIFY_COMMAND_SIZE(slot, sizeof(*port));

    if (*port)
        return dln2_response_error(slot, DLN2_RES_INVALID_PORT_NUMBER);

    memset(data, 0, 1 + 36);
    j = 1;
    for (i = 4; i <= 16; i++)
        data[j++] = i;
    data[0] = j - 1;

    return dln2_response(slot, 1 + 36);
}

static bool dln2_spi_get_ss_count(struct dln2_slot *slot)
{
    uint8_t *port = dln2_slot_header_data(slot);

    LOG1("DLN2_SPI_GET_SS_COUNT: port=%u\n", *port);
    DLN2_VERIFY_COMMAND_SIZE(slot, sizeof(*port));

    if (*port)
        return dln2_response_error(slot, DLN2_RES_INVALID_PORT_NUMBER);

    // set defaults
    dln2_spi_config.freq = DLN2_SPI_DEFAULT_FREQUENCY;
    dln2_spi_config.bpw = 8;

    return dln2_response_u16(slot, 1);
}

static uint dln2_spi_get_frequency(struct dln2_slot *slot, uint32_t freq)
{
    struct dln2_header *hdr = dln2_slot_header(slot);
    uint8_t *port = dln2_slot_header_data(slot);

    LOG1("%s: port=%u freq=%u\n",
         hdr->id == DLN2_SPI_GET_MIN_FREQUENCY ? "DLN2_SPI_GET_MIN_FREQUENCY" : "DLN2_SPI_GET_MAX_FREQUENCY" , *port, freq);
    DLN2_VERIFY_COMMAND_SIZE(slot, 1);

    if (*port)
        return dln2_response_error(slot, DLN2_RES_INVALID_PORT_NUMBER);

    return dln2_response_u32(slot, freq);
}

bool dln2_handle_spi(struct dln2_slot *slot)
{
    struct dln2_header *hdr = dln2_slot_header(slot);

    switch (hdr->id) {
    case DLN2_SPI_ENABLE:
        return dln2_spi_enable(slot, true);
    case DLN2_SPI_DISABLE:
        return dln2_spi_enable(slot, false);
    case DLN2_SPI_SET_MODE:
        return dln2_spi_set_mode(slot);
    case DLN2_SPI_SET_FRAME_SIZE:
        return dln2_spi_set_bpw(slot);
    case DLN2_SPI_SET_FREQUENCY:
        return dln2_spi_set_frequency(slot);
    case DLN2_SPI_READ_WRITE:
        return dln2_spi_read_write(slot);
    case DLN2_SPI_READ:
        return dln2_spi_read(slot);
    case DLN2_SPI_WRITE:
        return dln2_spi_write(slot);
    case DLN2_SPI_SET_SS:
        return dln2_spi_set_ss(slot);
    case DLN2_SPI_SS_MULTI_ENABLE:
        return dln2_spi_ss_multi_enable(slot, true);
    case DLN2_SPI_SS_MULTI_DISABLE:
        return dln2_spi_ss_multi_enable(slot, false);
    case DLN2_SPI_GET_SUPPORTED_FRAME_SIZES:
        return dln2_spi_get_supported_frame_sizes(slot);
    case DLN2_SPI_GET_SS_COUNT:
        return dln2_spi_get_ss_count(slot);
    case DLN2_SPI_GET_MIN_FREQUENCY:
        return dln2_spi_get_frequency(slot, dln2_spi_min_frequency());
    case DLN2_SPI_GET_MAX_FREQUENCY:
        return dln2_spi_get_frequency(slot, dln2_spi_max_frequency());
    default:
        LOG1("SPI: unknown command 0x%02x\n", hdr->id);
        return dln2_response_error(slot, DLN2_RES_COMMAND_NOT_SUPPORTED);
    }
}
