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
    i2c-at24.c
    i2c-at24-flash.c
)

target_include_directories(dln2 PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(dln2 PRIVATE pico_stdlib tinyusb_device tinyusb_board pico_unique_id hardware_gpio hardware_i2c hardware_spi hardware_adc)
pico_add_extra_outputs(dln2)
