cmake_minimum_required(VERSION 3.22)
project(secure_boot C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(BUILD_HOST_TESTS "Build host unit tests" OFF)

set(STRICT_WARNINGS -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wdouble-promotion -Werror)

if(BUILD_HOST_TESTS)
  enable_testing()
  add_subdirectory(tests)
else()
  add_subdirectory(third_party)
  add_subdirectory(bootloader)
  add_subdirectory(smoke)
endif()
