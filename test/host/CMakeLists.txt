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
    ../../src/cast_chunker.c
    ../../src/cast_reassembler.c
)
target_include_directories(component PUBLIC
    ../../include
    ../../src
    mocks
)

# Test data directory
add_compile_definitions(TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# Test executable
add_executable(host_test
    test_cast_chunker.cpp
    test_cast_reassembler.cpp
)
target_link_libraries(host_test PRIVATE component GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(host_test)
