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
import gpiod
import sys
import errno
from pathlib import Path
import subprocess
import time

gpio_unavail = (24, 23, 1, 0)

# the gpio's are connected through a 330 ohm resistor
gpio_pairs = [(2, 3),
              (6, 7),
              (8, 9),
              (10, 11),
              (12, 13),
              (14, 15),
              (27, 28),
              ]

gpio_pairs_flipped = [(pair[1], pair[0]) for pair in gpio_pairs]

gpio_pairs_all = gpio_pairs + gpio_pairs_flipped

@pytest.fixture(scope='module')
def chip():
    return gpiod.Chip('dln2')

def gpio_request(chip, gpio, typ):
    line = chip.get_line(gpio)
    label = Path(sys.argv[0]).name
    # TODO: >=v1.5: default_vals deprecated, use default_val=1
    line.request(consumer=label, type=typ, default_vals=(1,)) # Set High to match inputs that are pulled high
    return line

def gpio_request_pair(chip, in_gpio, out_gpio, interrupt=False):
    #print('gpio_request_pair:', in_gpio, out_gpio)
    in_line = gpio_request(chip, in_gpio, gpiod.LINE_REQ_EV_BOTH_EDGES if interrupt else gpiod.LINE_REQ_DIR_IN)
    out_line = gpio_request(chip, out_gpio, gpiod.LINE_REQ_DIR_OUT)
    return in_line, out_line

def gpio_in_out(chip, in_gpio, out_gpio):
    i, o = gpio_request_pair(chip, in_gpio, out_gpio)

    try:
        for out in [0, 1]:
            o.set_value(out)
            v = i.get_value()
            #print('v:', v)
            assert v == out
    finally:
        i.release()
        o.release()

def gpio_interrupt(chip, in_gpio, out_gpio):
    i, o = gpio_request_pair(chip, in_gpio, out_gpio, interrupt=True)

    # there's a pull-up on 'i'

    try:
        for out in [0, 1]:
            o.set_value(out)
            assert i.event_wait(1), 'No interrupt received'
            event = i.event_read()
            #print('event:', event)
            if out:
                assert event.type == gpiod.LineEvent.RISING_EDGE
            else:
                assert event.type == gpiod.LineEvent.FALLING_EDGE
    finally:
        i.release()
        o.release()

@pytest.mark.parametrize('gpio', gpio_unavail)
def test_gpio_unavail(chip, gpio):
    line = chip.get_line(gpio)
    with pytest.raises(OSError) as excinfo:
        line.request(consumer=sys.argv[0], type=gpiod.LINE_REQ_DIR_IN)
    assert excinfo.value.errno == errno.EREMOTEIO

@pytest.mark.parametrize('pair', gpio_pairs_all, ids=[f'{pair[1]}->{pair[0]}' for pair in gpio_pairs_all])
def test_gpio_set_get(chip, pair):
    gpio_in_out(chip, pair[0], pair[1])

@pytest.mark.parametrize('pair', gpio_pairs_all, ids=[f'{pair[1]}->{pair[0]}' for pair in gpio_pairs_all])
def test_gpio_interrupt(chip, pair):
    gpio_interrupt(chip, pair[0], pair[1])

# Tests using an external source

# GP22 is connected to GPIO27 on the Pi4 through a 330 ohm resistor
ext_gpio_num = 27

@pytest.fixture(scope='module')
def ext_chip():
    return gpiod.Chip('pinctrl-bcm2711')

@pytest.fixture
def ext_gpio(ext_chip):
    line = gpio_request(ext_chip, ext_gpio_num, gpiod.LINE_REQ_DIR_OUT)
    yield line
    line.release()

def test_gpio_gp22_set_get(chip, ext_gpio):
    line = gpio_request(chip, 22, gpiod.LINE_REQ_DIR_IN)

    try:
        for out in [0, 1]:
            ext_gpio.set_value(out)
            v = line.get_value()
            #print('v:', v)
            assert v == out
    finally:
        line.release()

def assert_event(line, expected):
    assert line.event_wait(1), f'Missing interrupt for event={expected}'
    event = line.event_read()
    #print('event:', event)
    assert event.type == expected
    assert not line.event_wait(1), 'Too many interrupts'

edges = (gpiod.LINE_REQ_EV_BOTH_EDGES, gpiod.LINE_REQ_EV_RISING_EDGE, gpiod.LINE_REQ_EV_FALLING_EDGE)
edges_ids = ['BOTH', 'RISING', 'FALLING']

@pytest.mark.parametrize('edge', edges, ids=edges_ids)
def test_gpio_gp22_interrupt(chip, ext_gpio, edge):
    line = gpio_request(chip, 22, edge)

    try:
        ext_gpio.set_value(0)
        if edge in (gpiod.LINE_REQ_EV_BOTH_EDGES, gpiod.LINE_REQ_EV_FALLING_EDGE):
            assert_event(line, gpiod.LineEvent.FALLING_EDGE)
        else:
            assert not line.event_wait(1), 'Should not have received interrupt'

        ext_gpio.set_value(1)
        if edge in (gpiod.LINE_REQ_EV_BOTH_EDGES, gpiod.LINE_REQ_EV_RISING_EDGE):
            assert_event(line, gpiod.LineEvent.RISING_EDGE)
        else:
            assert not line.event_wait(1), 'Should not have received interrupt'
    finally:
        line.release()

def run_gpio_pulse(req):
    prog = Path(req.node.fspath).parent.joinpath('gpio_pulse/gpio_pulse')
    if not prog.is_file():
        pytest.skip('gpio_pulse is not built')
    res = subprocess.run([prog, str(ext_gpio_num)])
    if res.returncode:
        print(res)
        print(res.stdout)
    assert res.returncode == 0

# Pulse the gpio so fast that the Pico detects both a fall and a rise event before its interrupt routine is called
@pytest.mark.parametrize('edge', edges, ids=edges_ids)
def test_gpio_gp22_interrupt_short_pulse(request, chip, ext_gpio, edge):
    line = gpio_request(chip, 22, edge)

    try:
        run_gpio_pulse(request)
        assert not line.event_wait(1), 'Should not have received interrupt'
    finally:
        line.release()

def count_interrupts(in_line, out_line, delay, num):
    for i in range(num):
        out_line.set_value(0)
        time.sleep(delay)
        out_line.set_value(1)
        time.sleep(delay)

    count = 0
    while True:
        if not in_line.event_wait(1):
            break
        event = in_line.event_read()
        #print('event:', event)
        count += 1
    return count

@pytest.mark.parametrize('edge', edges, ids=edges_ids)
def test_gpio_gp22_interrupt_multiple(chip, ext_gpio, edge):
    line = gpio_request(chip, 22, edge)

    try:
        # kernel event fifo size is 16, more than that and they will be dropped
        count = count_interrupts(line, ext_gpio, 10 / 1000, 8)
        if edge == gpiod.LINE_REQ_EV_BOTH_EDGES:
            assert count == 16
        else:
            assert count == 8
    finally:
        line.release()

# Make sure the device runs out of slots and the events array is maxed out, then make sure it has recovered
def test_gpio_gp22_interrupt_run_out_of_slots(chip, ext_gpio):
    line = gpio_request(chip, 22, gpiod.LINE_REQ_EV_RISING_EDGE)

    try:
        sleep_us = 1
        for i in range(10000):
            ext_gpio.set_value(0)
            time.sleep(sleep_us / 1000000)
            ext_gpio.set_value(1)
            time.sleep(sleep_us / 1000000)

        count = 0
        while True:
            if not line.event_wait(1):
                break
            event = line.event_read()
            #print('event:', event)
            count += 1

        #print('count:', count)
        assert count > 0

        for val in [0, 1]:
            ext_gpio.set_value(val)
            assert line.get_value() == val
        assert line.event_wait(1)
        event = line.event_read()
        #print('LAST event:', event)
        assert not line.event_wait(1), 'Too many interrupts'

    finally:
        line.release()
