add_executable(bootloader
  src/main.c src/boot.c src/boot_check.c src/header.c src/crc32.c
  port/board.c port/crc_hw_port.c port/flash_port.c)
target_include_directories(bootloader PRIVATE include port ${CMAKE_SOURCE_DIR}/common)
target_link_libraries(bootloader PRIVATE cmsis_startup cmsis_system)
target_compile_options(bootloader PRIVATE ${STRICT_WARNINGS} -Os -g)
target_link_options(bootloader PRIVATE -T${CMAKE_CURRENT_SOURCE_DIR}/linker/bootloader.ld
  --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections -Wl,-Map=bootloader.map)
add_custom_command(TARGET bootloader POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O binary bootloader bootloader.bin
  COMMAND ${CMAKE_SIZE} bootloader)
