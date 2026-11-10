add_library(Switch SwitchController.cpp)
target_link_libraries(Switch pico_stdlib controller )
target_include_directories(Switch PUBLIC ${PROJECT_SOURCE_DIR}/include)
