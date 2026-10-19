add_executable(smoke main.c)
target_link_libraries(smoke PRIVATE cmsis_device)
target_compile_options(smoke PRIVATE ${STRICT_WARNINGS} -Os -g)
target_link_options(smoke PRIVATE -T${CMAKE_SOURCE_DIR}/bootloader/linker/bootloader.ld
  --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections -Wl,-Map=smoke.map)
add_custom_command(TARGET smoke POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O binary smoke smoke.bin
  COMMAND ${CMAKE_SIZE} smoke)
