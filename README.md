# Raspberry Pi Pico USB I/O Board

This project turns the Raspberry Pi Pico into a USB I/O Board.

It implements parts of the DLN protocol used in the Linux drivers that targets the [DLN-2 USB-I2C/SPI/GPIO Adapter](https://diolan.com/dln-2).

<img src="./pinout.svg" width="50%">

Note: UART is currently used for debugging purposes

See [wiki](https://github.com/notro/pico-usb-io-board/wiki) for more information.


# Build
```
$ cd pico-usb-io-board
$ mkdir build
$ cd build
$ cmake ..
$ make

```

The ```PICO_SDK_PATH``` environment variable should point to the Pico SDK.


# License

Unless otherwise stated, all code and data is licensed under a [CC0 license](https://creativecommons.org/publicdomain/zero/1.0/).

The [Openmoko Product ID](https://github.com/openmoko/openmoko-usb-oui) [1d50:6170](https://github.com/openmoko/openmoko-usb-oui/pull/35) can only be used under a [FOSS license](https://github.com/openmoko/openmoko-usb-oui#conditions).

["Raspberry Pi Pico Pinout Diagram"](https://www.raspberrypi.com/documentation/microcontrollers/images/Pico-R3-SDK11-Pinout.svg) by [Raspberry Pi (Trading) Ltd](https://www.raspberrypi.com/), used under [CC BY-SA](https://creativecommons.org/licenses/by-sa/4.0/) / Pinout simplified from original
