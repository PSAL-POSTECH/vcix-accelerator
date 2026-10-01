cmake_minimum_required(VERSION 3.16)
project(vcix_accelerator CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(vcix_accel INTERFACE)
target_include_directories(vcix_accel INTERFACE include)

add_library(print_args SHARED examples/print_args/print_args.cc)
target_link_libraries(print_args PRIVATE vcix_accel)

# The Spike adapter is the only target that needs a simulator tree: Spike
# installs no headers and does not export extension_t's typeinfo, so it is built
# against the source tree and links libriscv.a the way Spike's own customext does.
set(SPIKE_SRC "" CACHE PATH "riscv-isa-sim source tree")
set(SPIKE_BUILD "" CACHE PATH "riscv-isa-sim build directory (holds config.h)")
if(SPIKE_SRC AND SPIKE_BUILD)
  # The adapter reads the machine description; a model never does.
  include(FetchContent)
  set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG 0.8.0)
  FetchContent_MakeAvailable(yaml-cpp)

  add_library(vcix_spike SHARED adapters/spike/vcix_spike.cc)
  target_include_directories(vcix_spike PRIVATE
    ${SPIKE_SRC}/riscv ${SPIKE_SRC}/softfloat ${SPIKE_SRC}/fesvr ${SPIKE_SRC} ${SPIKE_BUILD})
  target_link_libraries(vcix_spike PRIVATE vcix_accel yaml-cpp::yaml-cpp ${SPIKE_BUILD}/libriscv.a ${CMAKE_DL_LIBS})
endif()
