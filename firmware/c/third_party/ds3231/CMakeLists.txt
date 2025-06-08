add_library(pico_ds3231 ds3231.h ds3231.c at24c32.c)

target_link_libraries(pico_ds3231 hardware_i2c hardware_gpio)

target_include_directories(pico_ds3231 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")