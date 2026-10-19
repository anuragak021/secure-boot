# Two demo apps linked for the app slot: fast (LD1, 100 ms) and slow (LD3, 1000 ms).
find_package(Python3 REQUIRED COMPONENTS Interpreter)

function(add_demo_app name version led_port led_pin half_period_ms)
  add_executable(app_${name} main.c)
  target_link_libraries(app_${name} PRIVATE cmsis_startup cmsis_system_app)
  target_compile_definitions(app_${name} PRIVATE
    APP_NAME="${name}" APP_LED_PORT=${led_port} APP_LED_PIN=${led_pin}
    APP_HALF_PERIOD_MS=${half_period_ms}u)
  target_compile_options(app_${name} PRIVATE ${STRICT_WARNINGS} -Os -g)
  target_link_options(app_${name} PRIVATE -T${CMAKE_CURRENT_SOURCE_DIR}/app.ld
    --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections -Wl,-Map=app_${name}.map)
  add_custom_command(TARGET app_${name} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary app_${name} app_${name}.bin
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/mkimage.py app_${name}.bin
            -o app_${name}.img --version ${version}
    COMMAND ${CMAKE_SIZE} app_${name})
endfunction()

add_demo_app(fast 1.0.0 GPIOB 0 100)
add_demo_app(slow 2.0.0 GPIOB 14 1000)
