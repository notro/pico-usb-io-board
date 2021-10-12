# SPDX-License-Identifier: CC0-1.0

import pytest
import gpiod
import sys
import errno

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

def gpio_request_pair(chip, _in, out, int=False):
    #print('gpio_request_pair:', _in, out)
    in_line = chip.get_line(_in)
    out_line = chip.get_line(out)
    in_line.request(consumer=sys.argv[0], type=gpiod.LINE_REQ_EV_BOTH_EDGES if int else gpiod.LINE_REQ_DIR_IN)
    out_line.request(consumer=sys.argv[0], type=gpiod.LINE_REQ_DIR_OUT)
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
    i, o = gpio_request_pair(chip, in_gpio, out_gpio, int=True)

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
