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
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "i2c-at24.h"

#define LOG1    //printf
#define LOG2    //printf


//#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
//
//#define FLASH_PAGE_SIZE (1u << 8)
//#define FLASH_SECTOR_SIZE (1u << 12)
//#define FLASH_BLOCK_SIZE (1u << 16)


#define AT24_FLASH_SIZE             (16 * 1024)
#define AT24_FLASH_START            (PICO_FLASH_SIZE_BYTES - AT24_FLASH_SIZE)
#define AT24_FLASH_END              PICO_FLASH_SIZE_BYTES
#define AT24_FLASH_SECTOR_SIZE      FLASH_SECTOR_SIZE
#define AT24_FLASH_SECTOR_COUNT     (AT24_FLASH_SIZE / AT24_FLASH_SECTOR_SIZE)

#define AT24_FLASH_HEADER_MAGIC     0x224e8d1e

struct at24_flash_header {
    uint32_t magic;
    uint64_t wear;
    uint64_t version;
    uint16_t address;
    uint8_t pad_zero[8];
    uint16_t checksum;
} TU_ATTR_PACKED;

static_assert(sizeof(struct at24_flash_header) == 32, "");

#define AT24_FLASH_PAGE_SIZE        (AT24_FLASH_SECTOR_SIZE - sizeof(struct at24_flash_header))

struct at24_flash_sector {
    struct at24_flash_header header;
    uint8_t data[AT24_FLASH_PAGE_SIZE];
};

static struct at24_flash_sector write_sector;
static uint32_t allocated_sector_offset;

static uint32_t flash_sector_index(const void *sector)
{
    return ((uint32_t)sector - XIP_BASE - AT24_FLASH_START) / AT24_FLASH_SECTOR_SIZE;
}

static void flash_print_header(const struct at24_flash_header *hdr)
{
    uint8_t pad_zero = 0;

    for (uint i = 0; i < sizeof(hdr->pad_zero); i++)
        pad_zero |= hdr->pad_zero[i];

    LOG1("%u: magic=%s wear=%llu version=%llu address=0x%02x pad_zero=%s checksum=%u\n",
           flash_sector_index(hdr), hdr->magic == AT24_FLASH_HEADER_MAGIC ? "yes" : "no",
           hdr->wear, hdr->version, hdr->address, pad_zero ? "no" : "yes", hdr->checksum);
}

static const void *flash_address(uint32_t flash_offs)
{
    return (const void *) (XIP_BASE + flash_offs);
}

static uint16_t flash_header_checksum(const struct at24_flash_header *hdr)
{
    uint len = sizeof(*hdr) - sizeof(uint16_t);
    uint8_t *buf = (uint8_t *)hdr;
    uint16_t sum = 0;

    for (uint i = 0; i < len; i++)
        sum += buf[i];
    return sum;
}

static bool flash_is_valid_sector(const struct at24_flash_sector *sector)
{
    const struct at24_flash_header *hdr = &sector->header;
    uint8_t pad_zero = 0;

    if (hdr->magic != AT24_FLASH_HEADER_MAGIC)
        return false;

    if (!hdr->wear || !hdr->version)
        return false;

    for (uint i = 0; i < sizeof(hdr->pad_zero); i++)
        pad_zero |= hdr->pad_zero[i];
    if (pad_zero)
        return false;

    return hdr->checksum == flash_header_checksum(hdr);
}

#define flash_for_each_sector(_offset, _start_index)  \
    for (_offset = AT24_FLASH_START + (_start_index * AT24_FLASH_SECTOR_SIZE); _offset < AT24_FLASH_END; _offset += AT24_FLASH_SECTOR_SIZE)

static const struct at24_flash_sector *find_flash_sector(uint16_t address)
{
    const struct at24_flash_sector *sector, *ret = NULL;
    uint64_t version = 0;
    uint32_t flash_offs;

    LOG1("%s: address=0x%02x\n", __func__, address);

    flash_for_each_sector(flash_offs, 0) {
        const struct at24_flash_sector *sector = flash_address(flash_offs);

        //flash_print_header(&sector->header);

        if (!flash_is_valid_sector(sector))
            continue;

        const struct at24_flash_header *hdr = &sector->header;
        if (hdr->address == address && hdr->version > version) {
            version = hdr->version;
            ret = sector;
        }
    }

    return ret;
}

static uint32_t find_free_flash_sector(uint64_t *wear)
{
    const struct at24_flash_sector *sector, *ret = NULL;
    uint32_t flash_offs;

    *wear = 0;

    // First see if there's a sector that has never been used
    flash_for_each_sector(flash_offs, 0) {
        const struct at24_flash_sector *sector = flash_address(flash_offs);

        flash_print_header(&sector->header);

        if (!flash_is_valid_sector(sector))
            return flash_offs;
    }

    // It's safe to reuse write_sector here
    uint16_t *addresses = (uint16_t *)&write_sector;
    unsigned int num_addresses = 0;

    // Find all i2c addresses in use
    flash_for_each_sector(flash_offs, 0) {
        const struct at24_flash_sector *sector = flash_address(flash_offs);
        const struct at24_flash_header *hdr = &sector->header;

        //flash_print_header(&sector->header);

        bool found = false;
        for (uint i = 0; i < num_addresses; i++) {
            if (addresses[i] == hdr->address) {
                found = true;
                break;
            }
        }

        if (!found)
            addresses[num_addresses++] = hdr->address;
    }

    uint64_t min_wear = ~0;
    uint32_t min_wear_flash_offs = 0;

    // Find the sector with the least wear
    for (uint i = 0; i < num_addresses; i++) {
        uint64_t version = 0;

        LOG1("  try address: 0x%02x\n", addresses[i]);

        // Find the current version so we know which one to ignore
        sector = find_flash_sector(addresses[i]);
        version = sector->header.version;
        LOG1("    version=%llu\n", version);

        // Find the sector with the least wear that is not in use
        flash_for_each_sector(flash_offs, 0) {
            const struct at24_flash_sector *sector = flash_address(flash_offs);
            const struct at24_flash_header *hdr = &sector->header;

            //flash_print_header(&sector->header);

            if (hdr->address != addresses[i])
                continue;

            if (hdr->version == version)
                continue;

            if (hdr->wear < min_wear) {
                min_wear = hdr->wear;
                min_wear_flash_offs = flash_offs;
            }
        }
    }

    if (min_wear_flash_offs)
        *wear = min_wear;

    return min_wear_flash_offs;
}

static uint32_t alloc_flash_sector(uint64_t *wear)
{
    uint32_t flash_offs = find_free_flash_sector(wear);
    if (!flash_offs)
        return 0;

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(flash_offs, AT24_FLASH_SECTOR_SIZE);
    restore_interrupts (ints);

    return flash_offs;
}

static void flash_sync(void)
{
    LOG1("FLASH SYNC:\n");

    LOG1("allocated_sector_offset=0x%x index=%u\n", allocated_sector_offset, (allocated_sector_offset - AT24_FLASH_START) / AT24_FLASH_SECTOR_SIZE);
    uint32_t ints = save_and_disable_interrupts();
    flash_range_program(allocated_sector_offset, (uint8_t *)&write_sector, AT24_FLASH_SECTOR_SIZE);
    restore_interrupts (ints);

    write_sector.header.address = 0;
    allocated_sector_offset = 0;
}

int i2c_at24_flash_read(const struct i2c_at24_device *at24, uint16_t address, unsigned int offset, void *buf, size_t len)
{
    if (write_sector.header.address && write_sector.header.address == address)
        flash_sync();

    const struct at24_flash_sector *sector = find_flash_sector(address);
    LOG1("AT24 FLASH READ: sector=%d\n", sector ? flash_sector_index(sector) : -1);
    if (!sector)
        return 0;

    i2c_at24_memcpy(buf, sector->data, offset, len, AT24_FLASH_PAGE_SIZE);

    return 1;
}

bool i2c_at24_flash_write(const struct i2c_at24_device *at24, uint16_t address, unsigned int offset, const void *buf, size_t len)
{
    uint16_t writing_address = write_sector.header.address;

    if (writing_address && writing_address != address)
        return false;

    if (!writing_address) {
        uint64_t wear, version;

        LOG1("%s: address=0x%02x\n", __func__, address);
        allocated_sector_offset = alloc_flash_sector(&wear);
        LOG1("allocated_sector_offset=0x%x index=%u\n", allocated_sector_offset, (allocated_sector_offset - AT24_FLASH_START) / AT24_FLASH_SECTOR_SIZE);
        if (!allocated_sector_offset)
            return false;

        const struct at24_flash_sector *sector = find_flash_sector(address);
        if (sector) {
            LOG1("%s: Replacing sector %u\n", __func__, flash_sector_index(sector));
            flash_print_header(&sector->header);
            version = sector->header.version + 1;
            memcpy(write_sector.data, sector->data, AT24_FLASH_PAGE_SIZE);
        } else {
            LOG1("%s: First version for this address\n", __func__);
            version = 1;
            memset(write_sector.data, 0xff, sizeof(write_sector.data));
            if (at24->initial_data && at24->initial_data_size && at24->initial_data_size <= sizeof(write_sector.data))
                memcpy(write_sector.data, at24->initial_data, at24->initial_data_size);
        }

        struct at24_flash_header *hdr = &write_sector.header;
        hdr->magic = AT24_FLASH_HEADER_MAGIC;
        hdr->wear = wear + 1;
        hdr->version = version;
        hdr->address = address;
        memset(hdr->pad_zero, 0, sizeof(hdr->pad_zero));
        hdr->checksum = flash_header_checksum(hdr);
    }

    memcpy(write_sector.data + offset, buf, len);

    if (offset + len == AT24_FLASH_SECTOR_SIZE)
        flash_sync();

    return true;
}
