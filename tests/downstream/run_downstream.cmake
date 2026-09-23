# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Summon Software Labs.
#
# Configure, build and run the downstream consumer against an installed
# Path Observatory package. Any failure is fatal: this script is the proof that
# find_package works from outside the build tree.

foreach(required PATHOBS_PREFIX PATHOBS_DOWNSTREAM_SOURCE PATHOBS_DOWNSTREAM_BINARY)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "${required} was not supplied")
  endif()
endforeach()

set(found_config FALSE)
foreach(candidate
    "${PATHOBS_PREFIX}/lib/cmake/PathObservatory/PathObservatoryConfig.cmake"
    "${PATHOBS_PREFIX}/lib64/cmake/PathObservatory/PathObservatoryConfig.cmake"
    "${PATHOBS_PREFIX}/share/cmake/PathObservatory/PathObservatoryConfig.cmake")
  if(EXISTS "${candidate}")
    set(found_config TRUE)
  endif()
endforeach()
if(NOT found_config)
  message(FATAL_ERROR "the installed package config was not found under ${PATHOBS_PREFIX}")
endif()

file(REMOVE_RECURSE "${PATHOBS_DOWNSTREAM_BINARY}")
file(MAKE_DIRECTORY "${PATHOBS_DOWNSTREAM_BINARY}")

set(configure_command "${CMAKE_COMMAND}"
    -S "${PATHOBS_DOWNSTREAM_SOURCE}"
    -B "${PATHOBS_DOWNSTREAM_BINARY}"
    "-DCMAKE_PREFIX_PATH=${PATHOBS_PREFIX}")
if(DEFINED PATHOBS_GENERATOR AND NOT PATHOBS_GENERATOR STREQUAL "")
  list(APPEND configure_command -G "${PATHOBS_GENERATOR}")
endif()
if(DEFINED PATHOBS_BUILD_TYPE AND NOT PATHOBS_BUILD_TYPE STREQUAL ""
   AND NOT PATHOBS_GENERATOR MATCHES "Visual Studio")
  list(APPEND configure_command "-DCMAKE_BUILD_TYPE=${PATHOBS_BUILD_TYPE}")
endif()
# A sanitizer instrumented static library must be consumed by an instrumented
# consumer; the annotation records would otherwise mismatch at link time.
if(DEFINED PATHOBS_DOWNSTREAM_ASAN AND PATHOBS_DOWNSTREAM_ASAN)
  list(APPEND configure_command "-DPATHOBS_ASAN=ON")
endif()

execute_process(COMMAND ${configure_command}
                RESULT_VARIABLE configure_result
                OUTPUT_VARIABLE configure_output
                ERROR_VARIABLE configure_error)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR "downstream configure failed:\n${configure_output}\n${configure_error}")
endif()

set(build_command "${CMAKE_COMMAND}" --build "${PATHOBS_DOWNSTREAM_BINARY}")
if(DEFINED PATHOBS_BUILD_TYPE AND NOT PATHOBS_BUILD_TYPE STREQUAL "")
  list(APPEND build_command --config "${PATHOBS_BUILD_TYPE}")
endif()
execute_process(COMMAND ${build_command}
                RESULT_VARIABLE build_result
                OUTPUT_VARIABLE build_output
                ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "downstream build failed:\n${build_output}\n${build_error}")
endif()

file(GLOB downstream_executables
     "${PATHOBS_DOWNSTREAM_BINARY}/downstream_consumer"
     "${PATHOBS_DOWNSTREAM_BINARY}/downstream_consumer.exe"
     "${PATHOBS_DOWNSTREAM_BINARY}/*/downstream_consumer.exe"
     "${PATHOBS_DOWNSTREAM_BINARY}/*/downstream_consumer")
list(LENGTH downstream_executables executable_count)
if(executable_count EQUAL 0)
  message(FATAL_ERROR "the downstream executable was not produced")
endif()
list(GET downstream_executables 0 downstream_executable)

execute_process(COMMAND "${downstream_executable}"
                RESULT_VARIABLE run_result
                OUTPUT_VARIABLE run_output
                ERROR_VARIABLE run_error)
if(NOT run_result EQUAL 0)
  message(FATAL_ERROR "downstream consumer failed:\n${run_output}\n${run_error}")
endif()

message(STATUS "downstream consumer output: ${run_output}")
