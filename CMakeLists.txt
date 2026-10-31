cmake_minimum_required(VERSION 3.16)
project(conduit CXX)

# Phase 5: sequential C++17 pipeline. Phase 7: optional OpenMP implementation.
# No other dependencies. Without OpenMP the sequential paths build and run.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

option(CONDUIT_OPENMP "Build the OpenMP implementation (Phase 7)" ON)

add_library(conduit
  src/corpus.cpp
  src/json.cpp
  src/sha256.cpp
  src/policy.cpp
  src/radix.cpp
  src/pipeline.cpp
  src/pipeline_omp.cpp)
target_include_directories(conduit PUBLIC include)
target_compile_options(conduit PRIVATE -Wall -Wextra -pedantic)

if(CONDUIT_OPENMP)
  # Apple clang ships no OpenMP runtime; point FindOpenMP at Homebrew's libomp.
  if(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    execute_process(COMMAND brew --prefix libomp
                    OUTPUT_VARIABLE LIBOMP_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET)
    if(LIBOMP_PREFIX AND EXISTS "${LIBOMP_PREFIX}/lib/libomp.dylib")
      set(OpenMP_CXX_FLAGS "-Xpreprocessor -fopenmp -I${LIBOMP_PREFIX}/include")
      set(OpenMP_CXX_LIB_NAMES omp)
      set(OpenMP_omp_LIBRARY "${LIBOMP_PREFIX}/lib/libomp.dylib")
    endif()
  endif()
  find_package(OpenMP COMPONENTS CXX)
  if(OpenMP_CXX_FOUND)
    target_link_libraries(conduit PUBLIC OpenMP::OpenMP_CXX)
    target_compile_definitions(conduit PUBLIC CONDUIT_HAVE_OPENMP)
    message(STATUS "conduit: OpenMP ${OpenMP_CXX_VERSION} enabled")
  else()
    message(WARNING "conduit: OpenMP not found; building sequential implementations only")
  endif()
endif()

foreach(tool conduit_run dump_access conduit_bench membw)
  add_executable(${tool} tools/${tool}.cpp)
  target_link_libraries(${tool} PRIVATE conduit)
  target_compile_options(${tool} PRIVATE -Wall -Wextra -pedantic)
endforeach()

add_executable(conduit_tests tests/test_main.cpp)
target_link_libraries(conduit_tests PRIVATE conduit)
target_compile_options(conduit_tests PRIVATE -Wall -Wextra -pedantic)

enable_testing()
add_test(NAME unit COMMAND conduit_tests)
