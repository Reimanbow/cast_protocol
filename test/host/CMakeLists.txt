cmake_minimum_required(VERSION 3.16)
project(host_test CXX C)

set(CMAKE_CXX_STANDARD 17)

# GoogleTest
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)
FetchContent_MakeAvailable(googletest)

# Component source (compiled as C)
add_library(component STATIC
    ../../src/cast_protocol.c
)
target_include_directories(component PUBLIC
    ../../include
    mocks
)

# Test executable
add_executable(host_test
    test_cast_protocol.cpp
)
target_link_libraries(host_test PRIVATE component GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(host_test)
