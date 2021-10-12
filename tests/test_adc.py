# SPDX-License-Identifier: CC0-1.0

import pytest
import sys
import time
import gpiod
import iio
from periphery import PWM

# there's a 330 ohm resistor connected between gp27 and gp28
channel_gpio_pairs = [(1, 28), (2, 27)]

adc_max = 1023 # 10-bit
adc_var = adc_max * 0.05 # 5 percent

class LineContext:
    def __init__(self, chip, offset, type):
        self.line = chip.get_line(offset)
        self.type = type
    def __enter__(self):
        self.line.request(consumer=sys.argv[0], type=self.type)
        return self.line
    def __exit__(self, exc_type, exc_value, exc_tb):
        self.line.release()

@pytest.fixture(scope='module')
def _iio_dev():
    ctx = iio.LocalContext()
    return ctx.find_device('dln2-adc')

@pytest.fixture()
def iio_dev(_iio_dev):
    for channel in _iio_dev.channels:
        channel.enabled = False
    return _iio_dev

@pytest.fixture(scope='module')
def gpio_dev():
    return gpiod.Chip('dln2')

@pytest.fixture()
def pwm():
    pwm = PWM(0, 0)
    pwm.frequency = 1000
    pwm.duty_cycle = 1
    pwm.enable()
    yield pwm
    pwm.disable()

@pytest.mark.parametrize('voltage_div', [i / 10 for i in range(1, 10)])
def test_adc0(iio_dev, pwm, voltage_div):
    pwm.duty_cycle = voltage_div
    time.sleep(0.3)
    ch = iio_dev.find_channel('voltage0')
    val = int(ch.attrs['raw'].value)
    #print('val:', val)
    nom = adc_max * voltage_div
    assert (nom - adc_var) < val < (nom + adc_var)

# only high,low testing for adc1 and 2
@pytest.mark.parametrize('ch, gpio', channel_gpio_pairs, ids=[f'ch{ch}:{gpio}' for ch, gpio in channel_gpio_pairs])
def test_gpio_adc1_adc2(iio_dev, gpio_dev, ch, gpio):
    channel = iio_dev.find_channel(f'voltage{ch}')

    with LineContext(gpio_dev, gpio, gpiod.LINE_REQ_DIR_OUT) as line:
        line.set_value(0)
        val = channel.attrs['raw'].value
        #print('val:', val)
        assert int(val) < adc_var

        line.set_value(1)
        val = channel.attrs['raw'].value
        #print('val:', val)
        assert int(val) > (adc_max - adc_var)

def test_buffer(iio_dev, gpio_dev, pwm):
    pwm.duty_cycle = 0.5
    time.sleep(0.3)

    for channel in iio_dev.channels:
        channel.enabled = True
    buf = iio.Buffer(iio_dev, 1)
    if buf is None:
        raise Exception("Unable to create buffer!\n")

    buf.refill()
    samples = buf.read()
    #print('samples:', len(samples), samples)
    ch0 = int.from_bytes(samples[:2], byteorder=sys.byteorder)
    ch1 = int.from_bytes(samples[2:4], byteorder=sys.byteorder)
    ch2 = int.from_bytes(samples[4:6], byteorder=sys.byteorder)
    print(f'ch0={ch0} (0x{ch0:04x}), ch1={ch1} (0x{ch1:04x}), ch2={ch2} (0x{ch2:04x})')

    nom = adc_max * 0.5
    assert (nom - adc_var) < ch0 < (nom + adc_var)

    time_ns = int.from_bytes(samples[-8:], byteorder=sys.byteorder)
    #print('timestamp:', ":".join("{:02x}".format(c) for c in samples[-8:]), time_ns)

    from datetime import datetime
    dt = datetime.fromtimestamp(time_ns // 1e9)
    print('dt:', dt)

@pytest.mark.parametrize('ch, gpio', channel_gpio_pairs, ids=[f'ch{ch}:{gpio}' for ch, gpio in channel_gpio_pairs])
def test_buffer_adc1_adc2(iio_dev, gpio_dev, ch, gpio, pwm):
    pwm.duty_cycle = 0.5
    time.sleep(0.3)

    channel0 = iio_dev.find_channel(f'voltage0')
    channel = iio_dev.find_channel(f'voltage{ch}')

    with LineContext(gpio_dev, gpio, gpiod.LINE_REQ_DIR_OUT) as line:
        line.set_value(1)

        channel0.enabled = True
        channel.enabled = True

        buf = iio.Buffer(iio_dev, 1)
        if buf is None:
            raise Exception("Unable to create buffer!\n")

        buf.refill()
        samples = buf.read()
        channel.enabled = False

        #print('samples:', len(samples), samples)
        ch0 = int.from_bytes(samples[:2], byteorder=sys.byteorder)
        ch = int.from_bytes(samples[2:4], byteorder=sys.byteorder)
        print(f'ch0={ch0} (0x{ch0:04x}), ch={ch} (0x{ch:04x})')

        nom = adc_max * 0.5
        assert (nom - adc_var) < ch0 < (nom + adc_var)
        assert (adc_max - adc_var) < ch <= adc_max
