cmake_minimum_required(VERSION 3.28.0)

# Keep Zephyr's toolchain cache inside the application repository.
set(USER_CACHE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../.cache/zephyr" CACHE PATH
    "Zephyr toolchain capability cache")
set(USE_CCACHE "0" CACHE STRING "Enable optional external compiler cache (0/1)")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(respmon_ztest LANGUAGES C CXX)

target_include_directories(app PRIVATE ../../include)
target_sources(app PRIVATE
    src/domain_tests.cpp
    ../../src/filter.cpp
    ../../src/sensor_health.cpp
    ../../src/state_machine.cpp
)
