cmake_minimum_required(VERSION 3.16)
project(vcix_accelerator CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(vcix_accel INTERFACE)
target_include_directories(vcix_accel INTERFACE include)

# A model library exports one symbol, vcix_accel_model, which
# VCIX_ACCEL_REGISTER makes visible by itself. Everything else is hidden, so
# two models loaded into one simulator cannot share symbols by accident.
function(vcix_accel_add_model name source)
  add_library(${name} SHARED ${source})
  target_link_libraries(${name} PRIVATE vcix_accel)
  set_target_properties(${name} PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
endfunction()

vcix_accel_add_model(print_args examples/print_args/print_args.cc)
vcix_accel_add_model(owns_one tests/ownership/owns_one.cc)
vcix_accel_add_model(pipelined tests/pipeline/pipelined.cc)
vcix_accel_add_model(shows_config tests/config/shows_config.cc)

# tests/two_models: one source built twice, so both libraries define a model
# class of the same global name; and once more with the compiler's default
# visibility, the way a model built outside this file may well be.
foreach(which 1 2)
  vcix_accel_add_model(same_name_${which} tests/two_models/same_name.cc)
  target_compile_definitions(same_name_${which} PRIVATE WHICH=${which})
  vcix_accel_add_model(same_name_default_${which} tests/two_models/same_name.cc)
  target_compile_definitions(same_name_default_${which} PRIVATE WHICH=${which})
  set_target_properties(same_name_default_${which} PROPERTIES CXX_VISIBILITY_PRESET default VISIBILITY_INLINES_HIDDEN OFF)
endforeach()
add_executable(load_two tests/two_models/load_two.cc)
target_link_libraries(load_two PRIVATE vcix_accel ${CMAKE_DL_LIBS})

add_executable(timing_probe tools/timing_probe.cc)
target_link_libraries(timing_probe PRIVATE vcix_accel ${CMAKE_DL_LIBS})

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
