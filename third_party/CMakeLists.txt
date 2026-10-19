# Vendor CMSIS (ST device headers, ARM core headers, startup, SystemInit).
# Vendor code is built without -Werror; our own code is strict.
add_library(cmsis_headers INTERFACE)
target_include_directories(cmsis_headers SYSTEM INTERFACE cmsis/device cmsis/core)
target_compile_definitions(cmsis_headers INTERFACE STM32H753xx)
target_compile_options(cmsis_headers INTERFACE ${MCU_FLAGS} -ffunction-sections -fdata-sections)
target_link_options(cmsis_headers INTERFACE ${MCU_FLAGS})

add_library(cmsis_startup STATIC cmsis/device/startup_stm32h753xx.s)
target_link_libraries(cmsis_startup PUBLIC cmsis_headers)

# SystemInit as shipped: vector table stays at the reset default (bootloader).
add_library(cmsis_system STATIC cmsis/device/system_stm32h7xx.c)
target_link_libraries(cmsis_system PUBLIC cmsis_headers)

# SystemInit for the application: also points SCB->VTOR at the app slot (0x08020400).
add_library(cmsis_system_app STATIC cmsis/device/system_stm32h7xx.c)
target_link_libraries(cmsis_system_app PUBLIC cmsis_headers)
target_compile_definitions(cmsis_system_app PRIVATE USER_VECT_TAB_ADDRESS VECT_TAB_OFFSET=0x00020400U)

# Convenience bundle for the smoke test (bootloader style startup).
add_library(cmsis_device INTERFACE)
target_link_libraries(cmsis_device INTERFACE cmsis_startup cmsis_system)
