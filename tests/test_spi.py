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
import re

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
def spi_busnum():
    path = Path('/sys/class/spi_master')
    regex = re.compile(r'\d+')
    for p in path.iterdir():
        #print(p)
        with p.joinpath('device/modalias').open() as f:
            alias = f.read()
            #print('alias:', alias)
            if 'dln2-spi' in alias:
                return int(regex.search(p.name).group(0))

    raise OSError(errno.ENODEV, 'No DLN-2 SPI adapter found')

@pytest.fixture(scope='module')
def eeprom0(spi_busnum):
    #print('spi_busnum:', spi_busnum)
    cs = 0
    return Path(f'/sys/bus/spi/devices/spi{spi_busnum}.{cs}/eeprom')

def test_eeprom(eeprom0):
    print(eeprom0)
    print('write')
    data = eeprom_write_random(eeprom0, 65536)
    print('read')
    actual = eeprom_read(eeprom0)
    print('done')
    assert len(actual) == len(data)
    # Do a quick test first, the diff generation for the whole buffer is insanly slow if it differs.
    assert actual[:16] == data[:16]
    assert actual == data
