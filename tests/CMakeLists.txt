cmake_minimum_required(VERSION 3.28.0)
project(respmon_domain_tests LANGUAGES CXX)
enable_testing()
add_executable(domain_tests domain_tests.cpp ../src/filter.cpp
    ../src/sensor_simulator.cpp ../src/sensor_health.cpp ../src/state_machine.cpp)
target_include_directories(domain_tests PRIVATE ../include)
set_target_properties(domain_tests PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
target_compile_options(domain_tests PRIVATE
    -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti)
add_test(NAME respmon_domain COMMAND domain_tests)
add_executable(state_machine_tests state_machine_tests.cpp ../src/state_machine.cpp)
target_include_directories(state_machine_tests PRIVATE ../include)
set_target_properties(state_machine_tests PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
target_compile_options(state_machine_tests PRIVATE
    -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti)
add_test(NAME respmon_state_machine COMMAND state_machine_tests)

add_executable(sensor_health_tests sensor_health_tests.cpp ../src/sensor_health.cpp ../src/filter.cpp ../src/state_machine.cpp)
target_include_directories(sensor_health_tests PRIVATE ../include)
set_target_properties(sensor_health_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
target_compile_options(sensor_health_tests PRIVATE -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti)
add_test(NAME respmon_sensor_health COMMAND sensor_health_tests)
