cmake_minimum_required(VERSION 3.18)
project(conduit CXX)

# Phase 5: sequential C++17 pipeline. Phase 7: optional OpenMP implementation.
# Phase 8: optional CUDA implementation (needs the CUDA toolkit; CUB ships with
# it). Without OpenMP / CUDA the remaining paths build and run, and the GPU
# kernels still run through the host emulation (--impl gpu-emu).
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

option(CONDUIT_OPENMP "Build the OpenMP implementation (Phase 7)" ON)
option(CONDUIT_CUDA "Build the CUDA implementation (Phase 8)" ON)

set(CXX_WARN $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -pedantic>)

# CUDA must be enabled before the library target exists so the target picks
# up CMAKE_CUDA_ARCHITECTURES / CMAKE_CUDA_STANDARD.
set(CONDUIT_WITH_CUDA OFF)
if(CONDUIT_CUDA)
  include(CheckLanguage)
  check_language(CUDA)
  if(CMAKE_CUDA_COMPILER)
    # RTX 40xx laptop/desktop (Ada) = 89. Override with -DCMAKE_CUDA_ARCHITECTURES=86 etc.
    if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
      set(CMAKE_CUDA_ARCHITECTURES 89)
    endif()
    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    enable_language(CUDA)
    find_package(CUDAToolkit REQUIRED)
    set(CONDUIT_WITH_CUDA ON)
  else()
    message(WARNING "conduit: no CUDA compiler found; GPU kernels run only through the host "
                    "emulation (--impl gpu-emu)")
  endif()
endif()

add_library(conduit
  src/corpus.cpp
  src/json.cpp
  src/sha256.cpp
  src/policy.cpp
  src/radix.cpp
  src/pipeline.cpp
  src/pipeline_omp.cpp
  src/schedule.cpp
  src/execute.cpp
  src/gpu/gpu.cpp
  src/gpu/emu_pipeline.cpp)
target_include_directories(conduit PUBLIC include)
target_compile_options(conduit PRIVATE ${CXX_WARN})

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

if(CONDUIT_WITH_CUDA)
  target_sources(conduit PRIVATE src/gpu/cuda_pipeline.cu)
  target_compile_definitions(conduit PUBLIC CONDUIT_HAVE_CUDA)
  target_compile_options(conduit PRIVATE
    $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr -lineinfo>)
  target_link_libraries(conduit PUBLIC CUDA::cudart)
  message(STATUS "conduit: CUDA ${CMAKE_CUDA_COMPILER_VERSION} enabled, architectures "
                 "${CMAKE_CUDA_ARCHITECTURES}")
endif()

foreach(tool conduit_run dump_access conduit_bench membw conduit_gpu conduit_trace)
  add_executable(${tool} tools/${tool}.cpp)
  target_link_libraries(${tool} PRIVATE conduit)
  target_compile_options(${tool} PRIVATE -Wall -Wextra -pedantic)
endforeach()

add_executable(conduit_tests tests/test_main.cpp)
target_link_libraries(conduit_tests PRIVATE conduit)
target_compile_options(conduit_tests PRIVATE -Wall -Wextra -pedantic)

enable_testing()
add_test(NAME unit COMMAND conduit_tests)
