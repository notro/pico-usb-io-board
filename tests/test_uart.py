# SPDX-License-Identifier: CC0-1.0
#
# Written in 2023 by Noralf Trønnes <noralf@tronnes.org>
#
# To the extent possible under law, the author(s) have dedicated all copyright and related and
# neighboring rights to this software to the public domain worldwide. This software is
# distributed without any warranty.
#
# You should have received a copy of the CC0 Public Domain Dedication along with this software.
# If not, see <http://creativecommons.org/publicdomain/zero/1.0/>.

import pytest
import itertools
import os
from pathlib import Path
import serial

@pytest.fixture(scope='module')
def uarts():
    path = Path('/dev/serial/by-id')
    found = []
    for p in path.iterdir():
        if not 'Raspberry_Pi_Pico_USB_I_O_Board' in str(p):
            continue
        found.append(p.resolve())

    if len(found) != 2:
        raise OSError(errno.ENODEV, f'Found {len(data)} DLN-2 UARTs, expected 2')
    return sorted(found)

import time

def assert_received(send, receive, data, bytesize):
    if bytesize in (5, 6, 7):
        mask = (0x1f, 0x3f, 0x7f)[bytesize - 5]
        data = bytes([d & mask for d in data])
    send.write(data)
    received = receive.read(len(data))
    assert data == received


data_set_short = [
    b'A',
    b'B' * 2,
    b'C' * 3,
    b'D' * 4,
    b'E' * 5,
    b'\0',
    b'\n',
    b'\r',
    b'\n\r',
    b'\r\n',
    b'Hello World',
]
data_set_short_ids = [f'data{i}' for i in range(len(data_set_short))]

data_set_long_sizes = itertools.chain.from_iterable((2**i - 1, 2**i, 2**i + 1) for i in range(3, 13)) # 7,8,9,15,16,17,...,4095,4096,4097
data_set_long = [os.urandom(size) for size in data_set_long_sizes]
data_set_long_ids = [str(len(data)) for data in data_set_long]


@pytest.mark.parametrize('stopbits', [1, ]) # many test are failing when using 2 stopbits
@pytest.mark.parametrize('parity', [serial.PARITY_NONE, ]) # rpi serial0 doesn't support EVEN and ODD
@pytest.mark.parametrize('bytesize', [7, 8]) # rpi serial0 doesn't support 6 bits, 5 bits fails in many of the tests
# No point in testing lots of baudrates since the device "supports" everything,
# it just picks the closest it can do. Testing high speed is important to verify
# that the RX FIFO doesn't overflow.
@pytest.mark.parametrize('baudrate', [9600, 19200, 115200, 1152000])
class TestUart0:
    def connection(self, uarts, baudrate, bytesize, parity, stopbits, size):
        tty_uart0, tty_uart1 = uarts
        paritybits = 0 if parity == serial.PARITY_NONE else 1
        timeout = ((1 + bytesize + paritybits + stopbits) * size / baudrate) + 1.0
        uart0 = serial.Serial(str(tty_uart0), baudrate=baudrate, bytesize=bytesize, parity=parity, stopbits=stopbits, timeout=timeout)
        ser0 = serial.Serial('/dev/serial0', baudrate=baudrate, bytesize=bytesize, parity=parity, stopbits=stopbits, timeout=timeout)
        return ser0, uart0

    @pytest.mark.parametrize('data', data_set_short, ids=data_set_short_ids)
    def test_read_short(self, uarts, baudrate, bytesize, parity, stopbits, data):
        ser0, uart0 = self.connection(uarts, baudrate, bytesize, parity, stopbits, len(data))
        assert_received(ser0, uart0, data, bytesize)

    @pytest.mark.parametrize('data', data_set_short, ids=data_set_short_ids)
    def test_write_short(self, uarts, baudrate, bytesize, parity, stopbits, data):
        ser0, uart0 = self.connection(uarts, baudrate, bytesize, parity, stopbits, len(data))
        assert_received(uart0, ser0, data, bytesize)

    def test_read_write(self, uarts, baudrate, bytesize, parity, stopbits):
        ser0, uart0 = self.connection(uarts, baudrate, bytesize, parity, stopbits, len(data_set_short))
        send_data_set = data_set_short
        receive_data_set = reversed(data_set_short)
        for send_data, receive_data in zip(send_data_set, receive_data_set):
            assert_received(uart0, ser0, send_data, bytesize)
            assert_received(ser0, uart0, receive_data, bytesize)

    @pytest.mark.parametrize('data', data_set_long, ids=data_set_long_ids)
    def test_read_long(self, uarts, baudrate, bytesize, parity, stopbits, data):
        ser0, uart0 = self.connection(uarts, baudrate, bytesize, parity, stopbits, len(data))
        assert_received(ser0, uart0, data, bytesize)

    @pytest.mark.parametrize('data', data_set_long, ids=data_set_long_ids)
    def test_write_long(self, uarts, baudrate, bytesize, parity, stopbits, data):
        ser0, uart0 = self.connection(uarts, baudrate, bytesize, parity, stopbits, len(data))
        assert_received(uart0, ser0, data, bytesize)

# Uart1 has TX and RX connected through a 330 ohm resistor
@pytest.mark.parametrize('stopbits', [1, 2])
@pytest.mark.parametrize('parity', [serial.PARITY_NONE, serial.PARITY_EVEN, serial.PARITY_ODD])
@pytest.mark.parametrize('bytesize', [5, 6, 7, 8])
@pytest.mark.parametrize('baudrate', [9600, 19200, 115200, 576000])
class TestUart1:
    def connection(self, uarts, baudrate, bytesize, parity, stopbits, size):
        tty_uart0, tty_uart1 = uarts
        paritybits = 0 if parity == serial.PARITY_NONE else 1
        timeout = ((1 + bytesize + paritybits + stopbits) * size / baudrate) + 1.0
        uart1 = serial.Serial(str(tty_uart1), baudrate=baudrate, bytesize=bytesize, parity=parity, stopbits=stopbits, timeout=timeout)
        return uart1

    @pytest.mark.parametrize('data', data_set_short, ids=data_set_short_ids)
    def test_short(self, uarts, baudrate, bytesize, parity, stopbits, data):
        uart1 = self.connection(uarts, baudrate, bytesize, parity, stopbits, len(data))
        assert_received(uart1, uart1, data, bytesize)

    @pytest.mark.parametrize('data', data_set_long, ids=data_set_long_ids)
    def test_long(self, uarts, baudrate, bytesize, parity, stopbits, data):
        uart1 = self.connection(uarts, baudrate, bytesize, parity, stopbits, len(data))
        assert_received(uart1, uart1, data, bytesize)


@pytest.mark.skip()
def test_break(uarts):
        tty_uart0, tty_uart1 = uarts
        uart1 = serial.Serial(str(tty_uart1), baudrate=115200)
        ser0 = serial.Serial('/dev/serial0', baudrate=115200)
        uart1.send_break()
        # Detecting this BREAK was not easy so I dropped it for now, here are some pointers:
        # https://stackoverflow.com/questions/14803434/receive-read-break-condition-on-linux-serial-port
        # https://stackoverflow.com/questions/41595882/detect-serial-break-linux
        # https://github.com/pyserial/pyserial/issues/539
        # https://forums.raspberrypi.com/viewtopic.php?t=302553
        # https://elixir.bootlin.com/linux/latest/C/ident/TTY_BREAK
