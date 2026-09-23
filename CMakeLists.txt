# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Summon Software Labs.

cmake_minimum_required(VERSION 3.25)

project(PathObservatory
        VERSION 1.0.0
        DESCRIPTION "Vendor neutral observation of actual versus expected path behaviour"
        HOMEPAGE_URL "https://github.com/summonlabs/Path-Observatory"
        LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------

option(PATHOBS_BUILD_TOOLS "Build the pathobs command line tools" ON)
option(PATHOBS_BUILD_EXAMPLES "Build the Path Observatory examples" ON)
option(PATHOBS_BUILD_BENCHMARKS "Build the Path Observatory benchmarks" ON)
option(PATHOBS_BUILD_TESTS "Build the Path Observatory test suite" ON)
option(PATHOBS_WARNINGS_AS_ERRORS "Treat first party compiler warnings as errors" ON)
option(PATHOBS_ENABLE_ASAN "Build with AddressSanitizer where the toolchain supports it" OFF)
option(PATHOBS_BUILD_SHARED "Build Path Observatory as a shared library" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

if(PATHOBS_BUILD_TESTS)
  enable_testing()
endif()

# ---------------------------------------------------------------------------
# First party warning policy
#
# The policy is INTERFACE so every first party target (library, tools,
# examples, benchmarks, tests) inherits exactly the same strictness, and
# PRIVATE on each target so downstream consumers are never forced to adopt it.
# ---------------------------------------------------------------------------

add_library(pathobs_project_options INTERFACE)

if(MSVC)
  target_compile_options(pathobs_project_options INTERFACE
    /W4
    /permissive-
    /Zc:__cplusplus
    /Zc:preprocessor
    /Zc:ternary
    /utf-8
    /EHsc
    /MP)
  if(PATHOBS_WARNINGS_AS_ERRORS)
    target_compile_options(pathobs_project_options INTERFACE /WX)
  endif()
  # Deterministic binaries are easier to reason about and to compare.
  target_compile_options(pathobs_project_options INTERFACE /Brepro)
else()
  target_compile_options(pathobs_project_options INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast
    -Wcast-align -Wunused -Woverloaded-virtual -Wconversion -Wsign-conversion
    -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough)
  if(PATHOBS_WARNINGS_AS_ERRORS)
    target_compile_options(pathobs_project_options INTERFACE -Werror)
  endif()
endif()

if(PATHOBS_ENABLE_ASAN)
  if(MSVC)
    target_compile_options(pathobs_project_options INTERFACE /fsanitize=address)
    add_compile_definitions(PATHOBS_ASAN_ENABLED=1)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(pathobs_project_options INTERFACE
      -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(pathobs_project_options INTERFACE
      -fsanitize=address,undefined -fno-omit-frame-pointer)
    add_compile_definitions(PATHOBS_ASAN_ENABLED=1)
  else()
    message(WARNING "PATHOBS_ENABLE_ASAN requested but the toolchain is unsupported")
  endif()
endif()

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Core library
# ---------------------------------------------------------------------------

set(PATHOBS_CORE_SOURCES
  src/canonical.cpp
  src/codec.cpp
  src/compare.cpp
  src/digest.cpp
  src/divergence.cpp
  src/documents.cpp
  src/error.cpp
  src/expected.cpp
  src/finding.cpp
  src/freshness.cpp
  src/history.cpp
  src/json.cpp
  src/limits.cpp
  src/metrics.cpp
  src/observed.cpp
  src/persistence.cpp
  src/random.cpp
  src/runtime.cpp
  src/socket.cpp
  src/source.cpp
  src/synthetic.cpp
  src/time.cpp
  src/topology.cpp
  src/transport.cpp
  src/version.cpp
  src/worker.cpp)

add_library(pathobs_core STATIC ${PATHOBS_CORE_SOURCES})
add_library(PathObservatory::core ALIAS pathobs_core)

target_include_directories(pathobs_core
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

target_compile_features(pathobs_core PUBLIC cxx_std_20)
# The warning policy is a build tree concern only: it must not leak into the
# installed interface, or a downstream consumer would inherit -Werror.
target_link_libraries(pathobs_core PRIVATE
  $<BUILD_INTERFACE:pathobs_project_options>
  Threads::Threads)
target_compile_definitions(pathobs_core PRIVATE
  PATHOBS_BUILD_CONFIGURATION="${CMAKE_BUILD_TYPE}")

set_target_properties(pathobs_core PROPERTIES
  OUTPUT_NAME "pathobs"
  # The alias used inside the build tree must be the name the installed package
  # exports, or a downstream find_package consumer would look for
  # PathObservatory::pathobs_core and not find it.
  EXPORT_NAME "core"
  POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Tools, examples, benchmarks, tests
# ---------------------------------------------------------------------------

if(PATHOBS_BUILD_TOOLS)
  add_subdirectory(tools)
endif()

if(PATHOBS_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(PATHOBS_BUILD_BENCHMARKS)
  add_subdirectory(bench)
endif()

if(PATHOBS_BUILD_TESTS)
  add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Install and package
# ---------------------------------------------------------------------------

install(TARGETS pathobs_core
        EXPORT PathObservatoryTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(PATHOBS_BUILD_TOOLS)
  install(TARGETS pathobs pathobs_node
          EXPORT PathObservatoryTargets
          RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

install(EXPORT PathObservatoryTargets
        FILE PathObservatoryTargets.cmake
        NAMESPACE PathObservatory::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/PathObservatory)

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PathObservatoryConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/PathObservatoryConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/PathObservatory)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/PathObservatoryConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/PathObservatoryConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/PathObservatoryConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/PathObservatory)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/PathObservatory)

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

message(STATUS "Path Observatory ${PROJECT_VERSION}")
message(STATUS "  build type          : ${CMAKE_BUILD_TYPE}")
message(STATUS "  compiler            : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  warnings as errors  : ${PATHOBS_WARNINGS_AS_ERRORS}")
message(STATUS "  address sanitizer   : ${PATHOBS_ENABLE_ASAN}")
message(STATUS "  tools/examples/bench: ${PATHOBS_BUILD_TOOLS}/${PATHOBS_BUILD_EXAMPLES}/${PATHOBS_BUILD_BENCHMARKS}")
message(STATUS "  tests               : ${PATHOBS_BUILD_TESTS}")
