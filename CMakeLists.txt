cmake_minimum_required(VERSION 3.13)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(chunithm_pico_bridge C CXX ASM)

pico_sdk_init()

add_executable(chunithm_pico_bridge
    src/main.c
    src/uart_bridge.c
    src/usb_descriptors.c
    src/sega_protocol.c
    src/led_driver.c
    src/io4.c
    src/air.c
    src/calibration.c
)

target_include_directories(chunithm_pico_bridge PRIVATE src)

pico_generate_pio_header(
    chunithm_pico_bridge
    ${CMAKE_CURRENT_LIST_DIR}/src/sk6812.pio
)

target_link_libraries(chunithm_pico_bridge PRIVATE
    pico_stdlib
    pico_multicore
    hardware_adc
    hardware_uart
    hardware_pio
    hardware_dma
    hardware_clocks
    tinyusb_device
    tinyusb_board
    hardware_flash
    hardware_sync
)

pico_enable_stdio_usb(chunithm_pico_bridge 0)
pico_enable_stdio_uart(chunithm_pico_bridge 0)

pico_add_extra_outputs(chunithm_pico_bridge)

add_custom_target(copy_uf2_to_root ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_BINARY_DIR}/chunithm_pico_bridge.uf2"
        "${CMAKE_CURRENT_SOURCE_DIR}/chunithm_pico_bridge.uf2"
    DEPENDS chunithm_pico_bridge
)