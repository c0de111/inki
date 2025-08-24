# Minimum required version of CMake
cmake_minimum_required(VERSION 3.12)

set(PICO_BOARD pico_w)
set(PICO_WIFI 1)
set(PICO_CYW43_ARCH_DEFAULT "pico_cyw43_arch_lwip_threadsafe_background")

# Include the Pico SDK and set up the project
include(pico_sdk_import.cmake)
project(inki C CXX ASM)

# Set C/C++ standards
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the Pico SDK
pico_sdk_init()

# Add external libraries and dependencies as subdirectories
add_subdirectory(third_party/Config)    # ePaper-specific configuration utilities (third-party)
add_subdirectory(third_party/e-Paper)  # ePaper display libraries (third-party)
add_subdirectory(third_party/Fonts)    # Font resources for ePaper (third-party)
add_subdirectory(third_party/GUI)      # Drawing routines for ePaper (third-party)
add_subdirectory(third_party/ds3231)   # RTC driver (DS3231)

set(PICO_CYW43_ARCH_CONFIG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/c)
add_compile_definitions(PICO_CYW43_ARCH_THREADSAFE_BACKGROUND)
# set(PICO_CYW43_ARCH_CONFIG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/c)

add_executable(inki_slot0
    main.c          # Main application logic
    ${CMAKE_CURRENT_BINARY_DIR}/version.c  # Auto-generated version info
    wifi.c          # Wi-Fi handling logic
    ImageResources.c     # Image data for ePaper display
    debug.c         # Debug functions
    flash.c         # Persistent config handling
    webserver.c
    webserver_utils.c   # Utility functions extracted from webserver
    webserver_pages.c   # HTML page generation functions
    webserver_flash.c   # Flash operations for webserver
    ${CMAKE_CURRENT_BINARY_DIR}/header_slot0.c
)

add_executable(inki_slot1
    main.c          # Main application logic
    ${CMAKE_CURRENT_BINARY_DIR}/version.c  # Auto-generated version info
    wifi.c          # Wi-Fi handling logic
    ImageResources.c     # Image data for ePaper display
    debug.c         # Debug functions
    flash.c         # Persistent config handling
    webserver.c
    webserver_utils.c   # Utility functions extracted from webserver
    webserver_pages.c   # HTML page generation functions
    webserver_flash.c   # Flash operations for webserver
    ${CMAKE_CURRENT_BINARY_DIR}/header_slot1.c
)

add_executable(inki_bootloader
    bootloader.c
)

add_executable(inki_default_config
    default_config.c
)

pico_set_linker_script(inki_bootloader ${CMAKE_CURRENT_SOURCE_DIR}/inki_bootloader.ld)
pico_set_linker_script(inki_slot0 ${CMAKE_CURRENT_SOURCE_DIR}/inki_flash_slot0.ld)
pico_set_linker_script(inki_slot1 ${CMAKE_CURRENT_SOURCE_DIR}/inki_flash_slot1.ld)
# pico_set_linker_script(inki_default_config ${CMAKE_CURRENT_SOURCE_DIR}/inki_default_config.ld)
target_link_options(inki_default_config PRIVATE
    -nostartfiles
    -nostdlib
    -Wl,-T${CMAKE_CURRENT_SOURCE_DIR}/inki_default_config.ld  # Dein .ld-Skript
)
target_compile_definitions(pico_cyw43_arch_lwip_threadsafe_background INTERFACE
    PICO_CYW43_ARCH_THREADSAFE_BACKGROUND=1
)

# Add include directories for the project
target_include_directories(inki_slot0 PRIVATE
    third_party/e-Paper      # ePaper libraries
    third_party/GUI          # Drawing utilities for ePaper
    third_party/Fonts        # Font resources
    third_party/Config       # ePaper-specific configuration utilities
    ${CMAKE_CURRENT_LIST_DIR} # Current project directory
    ${CMAKE_CURRENT_SOURCE_DIR}          # damit das Projekt-Verzeichnis (z. B. `c/`) durchsucht wird
)

# Add include directories for the project
target_include_directories(inki_slot1 PRIVATE
    third_party/e-Paper      # ePaper libraries
    third_party/GUI          # Drawing utilities for ePaper
    third_party/Fonts        # Font resources
    third_party/Config       # ePaper-specific configuration utilities
    ${CMAKE_CURRENT_LIST_DIR} # Current project directory
    ${CMAKE_CURRENT_SOURCE_DIR}          # damit das Projekt-Verzeichnis (z. B. `c/`) durchsucht wird
)
# --- Git version and build date for firmware metadata ---
find_package(Git REQUIRED)

# Get Git version string, e.g. "v1.0.7-30-g3131d-dirty"
execute_process(
    COMMAND git describe --tags --always --dirty --abbrev=5
    OUTPUT_VARIABLE GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Get build date in YYYY-MM-DD format
string(TIMESTAMP BUILD_DATE "%Y-%m-%d")

# Print version info for logging/debugging
message(STATUS "Firmware Git version: ${GIT_VERSION}")
message(STATUS "Build date: ${BUILD_DATE}")

# --- Generate version.c from template ---
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/version_template.c.in
    ${CMAKE_CURRENT_BINARY_DIR}/version.c
    @ONLY
)

# --- Generate firmware header source files (slot 0 and slot 1) ---
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/header_slot0_template.c.in
    ${CMAKE_CURRENT_BINARY_DIR}/header_slot0.c
    @ONLY
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/header_slot1_template.c.in
    ${CMAKE_CURRENT_BINARY_DIR}/header_slot1.c
    @ONLY
)

target_sources(inki_slot0 PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/version.c
    ${CMAKE_CURRENT_BINARY_DIR}/header_slot0.c
)

target_sources(inki_slot1 PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/version.c
    ${CMAKE_CURRENT_BINARY_DIR}/header_slot1.c
)

option(USB_BOOTLOADER_ENABLE "Enable USB support in bootloader" OFF)

if(USB_BOOTLOADER_ENABLE)
pico_enable_stdio_usb(inki_bootloader 1)
    add_definitions(-DUSB_BOOTLOADER_ENABLE=1)
else()
pico_enable_stdio_usb(inki_bootloader 0)
endif()

pico_enable_stdio_uart(inki_bootloader 0)
pico_enable_stdio_usb(inki_slot0 1)
pico_enable_stdio_uart(inki_slot0 0)
pico_enable_stdio_usb(inki_slot1 1)
pico_enable_stdio_uart(inki_slot1 0)

target_link_libraries(inki_slot0
    ePaper                         # ePaper driver
    GUI                            # Drawing utilities for ePaper
    Fonts                          # Font resources for ePaper
    Config                         # ePaper-specific configuration utilities
    pico_stdlib                    # Standard Pico SDK library
    hardware_spi                   # SPI hardware support
    pico_cyw43_arch_lwip_threadsafe_background # Thread-safe Wi-Fi
    pico_ds3231                    # DS3231 RTC driver
    hardware_adc                   # ADC hardware support
    pico_rand                      # Random number utilities
    pico_lwip
)
target_link_libraries(inki_slot1
    ePaper                         # ePaper driver
    GUI                            # Drawing utilities for ePaper
    Fonts                          # Font resources for ePaper
    Config                         # ePaper-specific configuration utilities
    pico_stdlib                    # Standard Pico SDK library
    hardware_spi                   # SPI hardware support
    pico_cyw43_arch_lwip_threadsafe_background # Thread-safe Wi-Fi
    pico_ds3231                    # DS3231 RTC driver
    hardware_adc                   # ADC hardware support
    pico_rand                      # Random number utilities
    pico_lwip
)

target_link_libraries(inki_bootloader
    pico_stdlib
)
#[[
target_link_libraries(inki_default_config
    pico_stdlib
)]]


# pico_add_extra_outputs(inki_default_config)
