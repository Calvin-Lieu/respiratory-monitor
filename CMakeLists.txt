cmake_minimum_required(VERSION 3.28.0)

# Keep generated caches inside this repository, including in restricted builds.
set(USER_CACHE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.cache/zephyr" CACHE PATH
    "Zephyr toolchain capability cache")
# Zephyr's FindCcache checks the literal string "0", rather than CMake OFF.
set(USE_CCACHE "0" CACHE STRING "Enable optional external compiler cache (0/1)")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(respiratory_monitor)

target_include_directories(app PRIVATE include)
option(RESPMON_PIPELINE_STRESS_TEST "Delay monitoring startup to test queue overflow in QEMU" OFF)
if(RESPMON_PIPELINE_STRESS_TEST)
    target_compile_definitions(app PRIVATE RESPMON_PIPELINE_STRESS_TEST=1)
endif()
target_sources(app PRIVATE
    src/main.cpp
    src/pipeline.cpp
    src/filter.cpp
    src/sensor_health.cpp
    src/sensor_simulator.cpp
    src/state_machine.cpp
)
