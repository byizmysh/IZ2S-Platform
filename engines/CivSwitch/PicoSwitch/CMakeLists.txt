cmake_minimum_required(VERSION 3.25.1)

# Must be set before pico_sdk_import.cmake: the SDK selects the platform
# while the import is processed, so setting PICO_BOARD afterwards is ignored.
if(NOT DEFINED PICO_SDK_PATH)
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif()
if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD pico2)
endif()

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
include(${PICO_SDK_PATH}/tools/CMakeLists.txt)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(PICO_CXX_ENABLE_EXCEPTIONS 1)
# picotool (uf2 convert AND coprodis) access-violates on some hosts; the
# build packs UF2s itself from the .bin (tools/pack_uf2.py, byte-identical
# output). Coproprocessor disassembly annotation is lost - cosmetic only.
set(PICO_NO_PICOTOOL 1 CACHE INTERNAL "picotool unusable on this host")

project(civ7-pico C CXX ASM)

if(NOT DEFINED CIV7_TARGET_NAME)
    if(PICO_BOARD STREQUAL "pico2")
        set(CIV7_TARGET_NAME civ7-pico2)
    else()
        set(CIV7_TARGET_NAME civ7-pico1)
    endif()
endif()

pico_sdk_init()
add_subdirectory(src)
