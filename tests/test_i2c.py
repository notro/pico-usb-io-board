# SPDX-License-Identifier: CC0-1.0
#
# Written in 2021 by Noralf Trønnes <noralf@tronnes.org>
#
# To the extent possible under law, the author(s) have dedicated all copyright and related and
# neighboring rights to this software to the public domain worldwide. This software is
# distributed without any warranty.
#
# You should have received a copy of the CC0 Public Domain Dedication along with this software.
# If not, see <http://creativecommons.org/publicdomain/zero/1.0/>.

import pytest
import errno
from pathlib import Path
import os
import time

def eeprom(busnum, address):
    #print('i2c_busnum:', busnum)
    path = Path(f'/sys/class/i2c-adapter/i2c-{busnum}')

    dev = path.joinpath(f'{busnum}-{address:04x}')
    if not dev.is_dir():
        with path.joinpath('new_device').open(mode='w') as f:
            f.write(f'24c32 0x{address:x}\n')
        time.sleep(2)
        #print(f'Added 0x{address:x}')

    eeprom = dev.joinpath('eeprom')
    if not eeprom.is_file():
        raise OSError(errno.ENOENT, f'No eeprom found: {eeprom}')

    return eeprom

def eeprom_write_random(path, size):
    data = os.urandom(size)
    with path.open(mode='wb') as f:
        f.write(data)
    return data

def eeprom_read(path):
    with path.open(mode='rb') as f:
        data = f.read()
    return data

@pytest.fixture(scope='module')
def i2c_busnum():
    adapter_path = Path('/sys/class/i2c-adapter')
    for p in adapter_path.iterdir():
        #print(p)
        with p.joinpath('name').open() as f:
            name = f.read()
            #print('name:', name)
            if name.startswith('dln2-i2c'):
                return int(p.name.split('-')[1])

    raise OSError(errno.ENODEV, 'No DLN-2 i2c adapter found')

@pytest.fixture(scope='module')
def eeprom10(i2c_busnum):
    return eeprom(i2c_busnum, 0x10)

@pytest.fixture(scope='module')
def eeprom50(i2c_busnum):
    return eeprom(i2c_busnum, 0x50)

@pytest.mark.skip()
def test_eeprom10(eeprom10):
    #print('eeprom10:', eeprom10)
    # The last block (32 bytes) is missing (used for an internal header), read as 0xff
    print('write')
    data = eeprom_write_random(eeprom10, 4 * 1024 - 32)
    print('read')
    actual = eeprom_read(eeprom10)
    assert len(actual) - 32 == len(data)
    assert actual[:-32] == data
    assert actual[-32:] == b'\xff' * 32

def test_eeprom50(eeprom50):
    #print('eeprom50:', eeprom50)
    print('write')
    data = eeprom_write_random(eeprom50, 4 * 1024)
    print('read')
    actual = eeprom_read(eeprom50)
    assert len(actual) == len(data)
    assert actual == data
