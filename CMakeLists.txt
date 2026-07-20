cmake_minimum_required(VERSION 3.14)

# Detect ESP-IDF build system
if(ESP_PLATFORM)
    # ESP-IDF component: header-only, just expose include directory
    idf_component_register(INCLUDE_DIRS "include")
    return()
endif()

# Standalone CMake project
project(iotmp_embedded VERSION 1.0.1 LANGUAGES CXX)

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
