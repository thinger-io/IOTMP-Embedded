cmake_minimum_required(VERSION 3.14)
project(iotmp_embedded VERSION 1.0.0 LANGUAGES CXX)

# Header-only library
add_library(iotmp_embedded INTERFACE)
target_include_directories(iotmp_embedded INTERFACE include)
target_compile_features(iotmp_embedded INTERFACE cxx_std_17)

# Tests
option(IOTMP_BUILD_TESTS "Build tests" ON)
if(IOTMP_BUILD_TESTS)
    add_executable(test_protocol tests/native/test_protocol.cpp)
    target_link_libraries(test_protocol iotmp_embedded)

    add_executable(test_client tests/test_client.cpp)
    target_include_directories(test_client PRIVATE tests)
    target_link_libraries(test_client iotmp_embedded)
endif()
