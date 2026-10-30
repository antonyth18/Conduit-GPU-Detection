cmake_minimum_required(VERSION 3.16)
project(conduit CXX)

# Phase 5: single-threaded C++17 pipeline. No dependencies, no OpenMP, no CUDA.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

add_library(conduit
  src/corpus.cpp
  src/json.cpp
  src/sha256.cpp
  src/policy.cpp
  src/pipeline.cpp)
target_include_directories(conduit PUBLIC include)
target_compile_options(conduit PRIVATE -Wall -Wextra -pedantic)

foreach(tool conduit_run dump_access)
  add_executable(${tool} tools/${tool}.cpp)
  target_link_libraries(${tool} PRIVATE conduit)
  target_compile_options(${tool} PRIVATE -Wall -Wextra -pedantic)
endforeach()

add_executable(conduit_tests tests/test_main.cpp)
target_link_libraries(conduit_tests PRIVATE conduit)
target_compile_options(conduit_tests PRIVATE -Wall -Wextra -pedantic)

enable_testing()
add_test(NAME unit COMMAND conduit_tests)
