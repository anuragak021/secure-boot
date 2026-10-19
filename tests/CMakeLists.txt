# Host unit tests for the pure logic. Built with the same strict warnings as firmware.
set(SRC ${CMAKE_SOURCE_DIR}/bootloader/src)
set(INC ${CMAKE_SOURCE_DIR}/bootloader/include ${CMAKE_SOURCE_DIR}/common)

add_library(bl_logic STATIC
  ${SRC}/crc32.c ${SRC}/cobs.c ${SRC}/header.c ${SRC}/protocol_codec.c ${SRC}/updater.c ${SRC}/boot_check.c)
target_include_directories(bl_logic PUBLIC ${INC})
target_compile_options(bl_logic PRIVATE ${STRICT_WARNINGS} -fsanitize=address,undefined)
target_link_options(bl_logic PUBLIC -fsanitize=address,undefined)

add_library(fake_flash STATIC fake_flash.c)
target_link_libraries(fake_flash PUBLIC bl_logic)
target_compile_options(fake_flash PRIVATE ${STRICT_WARNINGS})

foreach(t crc32 cobs header protocol updater boot_check)
  add_executable(test_${t} test_${t}.c)
  target_link_libraries(test_${t} PRIVATE bl_logic fake_flash)
  target_compile_options(test_${t} PRIVATE ${STRICT_WARNINGS} -fsanitize=address,undefined)
  add_test(NAME ${t} COMMAND test_${t})
endforeach()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME python_tools
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/tests/test_tools.py)
