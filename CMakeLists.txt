cmake_minimum_required(VERSION 3.16)
project(vcix_accelerator CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(vcix_accel INTERFACE)
target_include_directories(vcix_accel INTERFACE include)

function(vcix_accel_add_model name source)
  add_library(${name} SHARED ${source})
  target_link_libraries(${name} PRIVATE vcix_accel)
  set_target_properties(${name} PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
endfunction()

vcix_accel_add_model(print_args examples/print_args/print_args.cc)
vcix_accel_add_model(tpu examples/tpu/tpu.cc)
vcix_accel_add_model(pipelined tests/pipeline/pipelined.cc)
vcix_accel_add_model(reports tests/contract/reports.cc)
vcix_accel_add_model(timing tests/contract/timing.cc)

# tests/contract: one source built twice under the same class name, hidden and not.
foreach(which 1 2)
  vcix_accel_add_model(same_name_${which} tests/contract/same_name.cc)
  target_compile_definitions(same_name_${which} PRIVATE WHICH=${which})
  vcix_accel_add_model(same_name_default_${which} tests/contract/same_name.cc)
  target_compile_definitions(same_name_default_${which} PRIVATE WHICH=${which})
  set_target_properties(same_name_default_${which} PROPERTIES CXX_VISIBILITY_PRESET default VISIBILITY_INLINES_HIDDEN OFF)
endforeach()

# tests/contract: a table written by hand, with `ready` and without; three libraries a caller must refuse.
vcix_accel_add_model(ready_only tests/contract/by_hand.cc)
target_compile_definitions(ready_only PRIVATE READY=1)
vcix_accel_add_model(no_optional tests/contract/by_hand.cc)
target_compile_definitions(no_optional PRIVATE READY=0)
vcix_accel_add_model(null_table tests/contract/refused.cc)
target_compile_definitions(null_table PRIVATE WHICH=1)
vcix_accel_add_model(other_abi tests/contract/refused.cc)
target_compile_definitions(other_abi PRIVATE WHICH=2)
vcix_accel_add_model(cannot_be_made tests/contract/refused.cc)
target_compile_definitions(cannot_be_made PRIVATE WHICH=3)

# tests/contract: the checks that need no simulator.
add_executable(direct tests/contract/direct.cc)
target_link_libraries(direct PRIVATE vcix_accel ${CMAKE_DL_LIBS})

add_executable(timing_probe tools/timing_probe.cc)
target_link_libraries(timing_probe PRIVATE vcix_accel ${CMAKE_DL_LIBS})

set(SPIKE_SRC "" CACHE PATH "riscv-isa-sim source tree")
set(SPIKE_BUILD "" CACHE PATH "riscv-isa-sim build directory (holds config.h)")
if(SPIKE_SRC AND SPIKE_BUILD)
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
