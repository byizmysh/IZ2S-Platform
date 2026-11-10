# macro_script.h is generated from payload.txt at build time; editing
# payload.txt and rebuilding is enough to update the macro.
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(PAYLOAD_FILE ${PROJECT_SOURCE_DIR}/payload.txt)
set(MACRO_SCRIPT ${PROJECT_SOURCE_DIR}/include/macro_script.h)

add_custom_command(
  OUTPUT ${MACRO_SCRIPT}
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/payload_to_macro.py ${PAYLOAD_FILE} ${MACRO_SCRIPT}
  DEPENDS ${PAYLOAD_FILE} ${PROJECT_SOURCE_DIR}/tools/payload_to_macro.py
  COMMENT "Generating macro_script.h from payload.txt"
)

add_library(macro MacroEngine.cpp ${MACRO_SCRIPT})
target_link_libraries(macro pico_stdlib hardware_pio tinyusb_device)
target_include_directories(macro PUBLIC ${PROJECT_SOURCE_DIR}/include)
