cmake_minimum_required(VERSION 3.13)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Konrad:
#set(PICO_BOARD pico_w CACHE STRING "Board target")
# Balazs:
set(PICO_BOARD pico2_w CACHE STRING "Board target")
set(PICO_PLATFORM "rp2350-arm-s" CACHE STRING "Pico platform")

include(pico_sdk_import.cmake)

project(my_pico_project C CXX ASM)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

pico_sdk_init()

add_executable(${PROJECT_NAME}
    src/api_server.cpp
    src/app.cpp
    src/base64.cpp
    src/mdns.cpp
    src/main.cpp
)

pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/src/ws2812.pio)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)

target_link_libraries(${PROJECT_NAME}
    pico_stdlib
    pico_lwip_mdns
    hardware_clocks
    hardware_pio
    hardware_dma
    hardware_sync
    pico_cyw43_arch_lwip_threadsafe_background
)

pico_add_extra_outputs(${PROJECT_NAME})
