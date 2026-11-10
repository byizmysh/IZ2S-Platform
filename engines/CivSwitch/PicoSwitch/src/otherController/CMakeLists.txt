add_library(controller INTERFACE)
target_link_libraries(controller INTERFACE pico_stdlib hardware_pio hardware_adc)
add_subdirectory(gamecube)
add_subdirectory(n64)
add_subdirectory(Switch)

pico_generate_pio_header(controller ${CMAKE_CURRENT_LIST_DIR}/Controller.pio)
