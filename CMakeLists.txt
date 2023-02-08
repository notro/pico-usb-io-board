cmake_minimum_required(VERSION 3.13)

#set(TINYUSB_DEBUG_LEVEL 2)

include(pico-sdk/pico_sdk_init.cmake)

project(dln2_project)

pico_sdk_init()

add_executable(dln2
    main.c
    usb_descriptors.c
    driver.c
    dln2.c
    dln2-pin.c
    dln2-gpio.c
    dln2-i2c.c
    dln2-spi.c
    dln2-adc.c
    cdc-uart.c
    i2c-at24.c
    i2c-at24-flash.c
)

target_include_directories(dln2 PRIVATE ${CMAKE_CURRENT_LIST_DIR})

# enable=1 to get debug output on uart0
pico_enable_stdio_uart(dln2 0)

target_link_libraries(dln2 PRIVATE
    pico_stdlib
    pico_unique_id
    tinyusb_board
    tinyusb_device
    hardware_adc
    hardware_gpio
    hardware_i2c
    hardware_spi
)

pico_add_extra_outputs(dln2)
