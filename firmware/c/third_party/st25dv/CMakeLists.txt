add_library(st25dv INTERFACE)

target_sources(st25dv INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/st25dv.c
    ${CMAKE_CURRENT_LIST_DIR}/st25dv_reg.c
)

target_include_directories(st25dv INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}
)
