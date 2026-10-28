cmake_minimum_required(VERSION 3.28.0)
project(respmon_domain_tests LANGUAGES CXX)
enable_testing()
add_executable(domain_tests domain_tests.cpp ../src/filter.cpp
    ../src/sensor_simulator.cpp ../src/state_machine.cpp)
target_include_directories(domain_tests PRIVATE ../include)
set_target_properties(domain_tests PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
target_compile_options(domain_tests PRIVATE
    -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti)
add_test(NAME respmon_domain COMMAND domain_tests)
