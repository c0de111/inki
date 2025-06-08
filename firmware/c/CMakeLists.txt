# Minimum required version of CMake
cmake_minimum_required(VERSION 3.12)

set(PICO_BOARD pico_w)
set(PICO_SDK_PATH "/home/nicolas/pico/pico-sdk")
set(PICO_WIFI 1)
set(PICO_CYW43_ARCH_DEFAULT "pico_cyw43_arch_lwip_threadsafe_background")

# Include the Pico SDK and set up the project
# include(pico_sdk_import.cmake)
include(${PICO_SDK_PATH}/pico_sdk_init.cmake)
project(esign C CXX ASM)

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

# Detect ROOM configuration from the environment or set a default
if(DEFINED ENV{ROOM})
    set(ROOM $ENV{ROOM} CACHE STRING "Room configuration for build" FORCE)
else()
    set(ROOM "LPB113H" CACHE STRING "Room configuration for build" FORCE)
endif()

# Add compile-time definition for ROOM
add_compile_definitions(${ROOM})
message(STATUS "Building for room: ${ROOM}")

add_compile_definitions(PICO_CYW43_ARCH_THREADSAFE_BACKGROUND)
set(PICO_CYW43_ARCH_CONFIG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/c)

# Define the executable and its source files
add_executable(esign
    main.c          # Main application logic
    version.c       # Versioning logic
    wifi.c          # Wi-Fi handling logic
    ImageResources.c     # Image data for ePaper display
    debug.c   # Add this if debug functions are implemented in debug.c
)

# Ensure ROOM changes trigger a rebuild
set_property(DIRECTORY PROPERTY CMAKE_CACHE_VARIABLES_TO_RESET ROOM)

target_compile_definitions(pico_cyw43_arch_lwip_threadsafe_background INTERFACE
    PICO_CYW43_ARCH_THREADSAFE_BACKGROUND=1
)

# Add include directories for the project
target_include_directories(esign PRIVATE
    third_party/e-Paper      # ePaper libraries
    third_party/GUI          # Drawing utilities for ePaper
    third_party/Fonts        # Font resources
    third_party/Config       # ePaper-specific configuration utilities
    ${CMAKE_CURRENT_LIST_DIR} # Current project directory
    ${PICO_SDK_PATH}/src/rp2_common/pico_cyw43_arch/include
    ${PICO_SDK_PATH}/lib/cyw43-driver/src
    ${PICO_SDK_PATH}/src/rp2_common/pico_async_context/include
    ${PICO_SDK_PATH}/lib/lwip/src/include
    ${PICO_SDK_PATH}/lib/lwip/src/include/compat/posix
    ${PICO_SDK_PATH}/lib/lwip/src/include/compat/arch
    ${PICO_SDK_PATH}/src/rp2_common/pico_lwip/include
    ${CMAKE_CURRENT_SOURCE_DIR}          # damit das Projekt-Verzeichnis (z. B. `c/`) durchsucht wird
)

# Precompute version components using Git (retaining the current setup)
find_package(Git REQUIRED)

# Get Git version string
execute_process(
    COMMAND git describe --tags --always --dirty --abbrev=5
    OUTPUT_VARIABLE GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Get the build date
string(TIMESTAMP BUILD_DATE "%Y-%m-%d")

# Log version and build date to console
message(STATUS "Version: ${GIT_VERSION}, Date: ${BUILD_DATE}")

# Generate version.c dynamically during the build process
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/version_template.c.in
    ${CMAKE_CURRENT_SOURCE_DIR}/version.c
    @ONLY
)

# Enable USB output, disable UART output
pico_enable_stdio_usb(esign 1)
pico_enable_stdio_uart(esign 0)

# Add support for extra output formats (map/bin/hex/uf2)
# pico_add_extra_outputs(esign)

# Link the required libraries to the project
target_link_libraries(esign
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
    # __force_wifi_arch
)
