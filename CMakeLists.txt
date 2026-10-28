cmake_minimum_required(VERSION 3.28.0)

# Keep generated caches inside this repository, including in restricted builds.
set(USER_CACHE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.cache/zephyr" CACHE PATH
    "Zephyr toolchain capability cache")
# Zephyr's FindCcache checks the literal string "0", rather than CMake OFF.
set(USE_CCACHE "0" CACHE STRING "Enable optional external compiler cache (0/1)")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(respiratory_monitor)

target_include_directories(app PRIVATE include)
target_sources(app PRIVATE
    src/main.cpp
    src/filter.cpp
    src/sensor_simulator.cpp
    src/state_machine.cpp
)
