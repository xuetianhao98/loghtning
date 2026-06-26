cmake_minimum_required(VERSION 3.20)

project(loghtning VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(LOGHTNING_BUILD_EXAMPLES "Build loghtning examples" ON)
option(LOGHTNING_BUILD_TESTS "Build loghtning tests" ON)
option(LOGHTNING_BUILD_BENCHMARKS "Build loghtning benchmarks" ON)

add_library(loghtning
  src/loghtning.cpp
)

add_library(loghtning::loghtning ALIAS loghtning)

target_compile_features(loghtning PUBLIC cxx_std_23)

target_include_directories(loghtning
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(MSVC)
  target_compile_options(loghtning PRIVATE /W4 /permissive-)
else()
  target_compile_options(loghtning PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(LOGHTNING_BUILD_EXAMPLES)
  add_executable(loghtning_basic examples/basic.cpp)
  target_link_libraries(loghtning_basic PRIVATE loghtning::loghtning)
endif()

if(LOGHTNING_BUILD_TESTS)
  enable_testing()

  find_package(GTest QUIET)
  if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(
      googletest
      URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
  endif()

  include(GoogleTest)

  add_executable(loghtning_tests tests/loghtning_tests.cpp)
  target_link_libraries(loghtning_tests PRIVATE loghtning::loghtning GTest::gtest_main)

  gtest_discover_tests(loghtning_tests)
endif()

if(LOGHTNING_BUILD_BENCHMARKS)
  find_package(benchmark QUIET)
  if(NOT benchmark_FOUND)
    include(FetchContent)
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_DOWNLOAD_DEPENDENCIES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
      googlebenchmark
      URL https://github.com/google/benchmark/archive/refs/tags/v1.9.5.zip
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(googlebenchmark)
  endif()

  add_executable(loghtning_benchmarks benchmarks/loghtning_benchmarks.cpp)
  target_link_libraries(
    loghtning_benchmarks
    PRIVATE
      loghtning::loghtning
      benchmark::benchmark_main
  )
endif()
