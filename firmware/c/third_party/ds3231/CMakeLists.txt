cmake_minimum_required(VERSION 3.12)

option(DS3231_BUILD_EXAMPLES "Build ds3231 example" OFF)

# Always expose the library for consumers
add_subdirectory(libraries/ds3231)

if(DS3231_BUILD_EXAMPLES)
  include(pico_sdk_import.cmake)
  project(pico-rtc C CXX ASM)
  set(CMAKE_C_STANDARD 11)
  set(CMAKE_CXX_STANDARD 17)
  pico_sdk_init()

  add_executable(pico-rtc ds3231_example.c)
  target_link_libraries(pico-rtc pico_stdlib pico_ds3231)
  pico_enable_stdio_uart(pico-rtc 0)
  pico_enable_stdio_usb(pico-rtc 1)
  pico_add_extra_outputs(pico-rtc)
endif()
