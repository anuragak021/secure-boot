# Vendor CMSIS (ST device headers, ARM core headers, startup, SystemInit).
# Vendor code is built without -Werror; our own code is strict.
add_library(cmsis_device STATIC
  cmsis/device/system_stm32h7xx.c
  cmsis/device/startup_stm32h753xx.s)
target_include_directories(cmsis_device SYSTEM PUBLIC cmsis/device cmsis/core)
target_compile_definitions(cmsis_device PUBLIC STM32H753xx)
target_compile_options(cmsis_device PUBLIC ${MCU_FLAGS} -ffunction-sections -fdata-sections)
target_link_options(cmsis_device PUBLIC ${MCU_FLAGS})
